#include "test.h"
#include "sim/events.h"

#include <cstring>

static DamageEvent test_damage(uint32_t target, int32_t amount, uint32_t source = UINT32_MAX) {
    DamageEvent event{};
    if (source != UINT32_MAX) event.source = EntityId{handle_make(source, 1u)};
    event.target = EntityId{handle_make(target, 1u)};
    event.amount = amount;
    return event;
}

TEST(sim_events, initialization_is_arena_backed_and_atomic) {
    alignas(16) uint8_t storage[256]{};
    size_t required = damage_event_queue_memory_required(4u);
    CHECK(required > 0u);
    CHECK(required <= sizeof(storage));
    Arena arena;
    arena_init_fixed(&arena, storage, required);
    DamageEventQueue queue{};
    CHECK(damage_event_queue_init(&queue, &arena, 4u));
    CHECK(damage_event_queue_is_valid(&queue));
    CHECK(queue.capacity == 4u);
    CHECK(queue.read_index == 0u);
    CHECK(queue.write_index == 1u);
    CHECK(queue.counts[0] == 0u);
    CHECK(queue.counts[1] == 0u);

    alignas(16) uint8_t short_storage[256]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, required - 1u);
    DamageEventQueue untouched{};
    untouched.capacity = 77u;
    DamageEventQueue before = untouched;
    CHECK(!damage_event_queue_init(&untouched, &short_arena, 4u));
    CHECK(std::memcmp(&untouched, &before, sizeof(before)) == 0);
    CHECK(short_arena.offset == 0u);
    CHECK(damage_event_queue_memory_required(0u) == 0u);
}

TEST(sim_events, append_publish_read_and_consume_preserve_literal_order) {
    alignas(16) uint8_t storage[256]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    DamageEventQueue queue{};
    CHECK(damage_event_queue_init(&queue, &arena, 4u));

    DamageEvent first = test_damage(7u, 11, 2u);
    DamageEvent second = test_damage(3u, 22);
    DamageEvent third = test_damage(7u, 33, 5u);
    CHECK(damage_event_queue_append(&queue, &first));
    CHECK(damage_event_queue_append(&queue, &second));
    CHECK(damage_event_queue_append(&queue, &third));

    DamageEventView before_publish{};
    CHECK(damage_event_queue_read(&queue, &before_publish));
    CHECK(before_publish.count == 0u);
    CHECK(damage_event_queue_publish(&queue));
    DamageEventView view{};
    CHECK(damage_event_queue_read(&queue, &view));
    CHECK(view.count == 3u);
    CHECK(view.events[0].target.h == first.target.h);
    CHECK(view.events[0].amount == first.amount);
    CHECK(view.events[1].target.h == second.target.h);
    CHECK(view.events[1].amount == second.amount);
    CHECK(view.events[2].source.h == third.source.h);
    CHECK(view.events[2].amount == third.amount);
    CHECK(damage_event_queue_consume(&queue));
    CHECK(damage_event_queue_read(&queue, &view));
    CHECK(view.count == 0u);
}

TEST(sim_events, next_tick_policy_uses_the_same_storage_phases) {
    alignas(16) uint8_t storage[128]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    DamageEventQueue queue{};
    CHECK(damage_event_queue_init(&queue, &arena, 2u));
    DamageEvent event = test_damage(1u, 9);
    CHECK(damage_event_queue_append(&queue, &event));

    // A scheduler can defer this publish to the next tick; until then the read
    // phase remains empty and the event stays intact in the write phase.
    DamageEventView view{};
    CHECK(damage_event_queue_read(&queue, &view));
    CHECK(view.count == 0u);
    CHECK(queue.counts[queue.write_index] == 1u);
    CHECK(damage_event_queue_publish(&queue));
    CHECK(damage_event_queue_read(&queue, &view));
    CHECK(view.count == 1u);
    CHECK(view.events[0].target.h == event.target.h);
}

TEST(sim_events, invalid_overflow_and_unread_publish_fail_without_mutation) {
    alignas(16) uint8_t storage[128]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    DamageEventQueue queue{};
    CHECK(damage_event_queue_init(&queue, &arena, 2u));
    DamageEvent first = test_damage(0u, 1);
    DamageEvent second = test_damage(1u, 2);
    CHECK(damage_event_queue_append(&queue, &first));
    CHECK(damage_event_queue_append(&queue, &second));

    DamageEventQueue before = queue;
    uint8_t bytes_before[128];
    std::memcpy(bytes_before, storage, sizeof(storage));
    DamageEvent overflow = test_damage(2u, 3);
    DamageEvent null_target{};
    DamageEvent negative = test_damage(2u, -1);
    CHECK(!damage_event_queue_append(&queue, &overflow));
    CHECK(!damage_event_queue_append(&queue, &null_target));
    CHECK(!damage_event_queue_append(&queue, &negative));
    CHECK(std::memcmp(&queue, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    CHECK(damage_event_queue_publish(&queue));
    before = queue;
    std::memcpy(bytes_before, storage, sizeof(storage));
    CHECK(!damage_event_queue_publish(&queue));
    CHECK(std::memcmp(&queue, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);
}

