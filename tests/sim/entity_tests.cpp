#include "test.h"
#include "sim/entity.h"

#include <cstring>

static EntityId test_entity(uint32_t index, uint32_t generation) {
    return EntityId{handle_make(index, generation)};
}

TEST(sim_entity, initialization_is_arena_backed_and_atomic) {
    alignas(16) uint8_t storage[128]{};
    Arena arena;
    arena_init_fixed(&arena, storage, entity_manager_memory_required(4u));
    EntityManager manager{};

    CHECK(entity_manager_init(&manager, &arena, 4u));
    CHECK(manager.capacity == 4u);
    CHECK(manager.live_count == 0u);
    CHECK(manager.next_fresh == 0u);
    CHECK(manager.free_count == 0u);
    CHECK(manager.generations != nullptr);
    CHECK(manager.liveness != nullptr);
    CHECK(manager.free_stack != nullptr);
    CHECK(arena.offset <= entity_manager_memory_required(4u));
    for (uint32_t i = 0; i < 4u; ++i) {
        CHECK(manager.generations[i] == 0u);
        CHECK(manager.liveness[i] == 0u);
    }

    alignas(16) uint8_t short_storage[8]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, sizeof(short_storage));
    EntityManager untouched{};
    untouched.capacity = 77u;
    EntityManager before = untouched;
    size_t offset_before = short_arena.offset;
    CHECK(!entity_manager_init(&untouched, &short_arena, 4u));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
    CHECK(short_arena.offset == offset_before);

    CHECK(!entity_manager_init(&untouched, &short_arena, 0u));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
    CHECK(short_arena.offset == offset_before);
    CHECK(entity_manager_memory_required(0u) == 0u);
    CHECK(entity_manager_memory_required(HANDLE_INDEX_MASK + 2u) == 0u);
}

TEST(sim_entity, fresh_allocation_ascends_and_exhaustion_is_atomic) {
    alignas(16) uint8_t storage[128]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    EntityManager manager{};
    CHECK(entity_manager_init(&manager, &arena, 4u));

    for (uint32_t index = 0; index < 4u; ++index) {
        EntityId entity = entity_manager_create(&manager);
        CHECK(entity.h == handle_make(index, 1u));
        CHECK(entity_manager_is_alive(&manager, entity));
    }

    EntityManager before = manager;
    uint8_t bytes_before[128];
    std::memcpy(bytes_before, storage, sizeof(storage));
    EntityId exhausted = entity_manager_create(&manager);
    CHECK(exhausted.h == HANDLE_NULL);
    CHECK(std::memcmp(&manager, &before, sizeof(manager)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);
}

TEST(sim_entity, released_slots_recycle_lifo_and_reject_stale_or_forged_ids) {
    alignas(16) uint8_t storage[128]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    EntityManager manager{};
    CHECK(entity_manager_init(&manager, &arena, 4u));

    EntityId entities[4]{};
    for (uint32_t i = 0; i < 4u; ++i) entities[i] = entity_manager_create(&manager);
    CHECK(entity_manager_release(&manager, entities[1]));
    CHECK(entity_manager_release(&manager, entities[3]));
    CHECK(!entity_manager_is_alive(&manager, entities[1]));
    CHECK(!entity_manager_is_alive(&manager, entities[3]));

    EntityManager before = manager;
    CHECK(!entity_manager_release(&manager, entities[1]));
    CHECK(std::memcmp(&manager, &before, sizeof(manager)) == 0);
    EntityId forged_free = test_entity(3u, manager.generations[3]);
    CHECK(!entity_manager_is_alive(&manager, forged_free));
    CHECK(!entity_manager_release(&manager, forged_free));
    CHECK(std::memcmp(&manager, &before, sizeof(manager)) == 0);

    EntityId recycled_three = entity_manager_create(&manager);
    EntityId recycled_one = entity_manager_create(&manager);
    CHECK(recycled_three.h == handle_make(3u, 2u));
    CHECK(recycled_one.h == handle_make(1u, 2u));
    CHECK(entity_manager_is_alive(&manager, recycled_three));
    CHECK(entity_manager_is_alive(&manager, recycled_one));
    CHECK(!entity_manager_is_alive(&manager, entities[3]));
    CHECK(!entity_manager_is_alive(&manager, entities[1]));
}

TEST(sim_entity, generations_preserve_all_adr_bits) {
    alignas(16) uint8_t storage[64]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    EntityManager manager{};
    CHECK(entity_manager_init(&manager, &arena, 1u));

    EntityId first = entity_manager_create(&manager);
    CHECK(entity_manager_release(&manager, first));
    manager.generations[0] = static_cast<uint16_t>(HANDLE_GEN_MASK - 1u);
    EntityId high = entity_manager_create(&manager);
    CHECK(handle_gen(high.h) == HANDLE_GEN_MASK - 1u);
    CHECK(entity_manager_release(&manager, high));
    EntityId highest = entity_manager_create(&manager);
    CHECK(handle_gen(highest.h) == HANDLE_GEN_MASK);
    CHECK(entity_manager_is_alive(&manager, highest));

#if defined(MOBA_RELEASE)
    CHECK(entity_manager_release(&manager, highest));
    EntityId wrapped = entity_manager_create(&manager);
    CHECK(handle_gen(wrapped.h) == 1u);
    CHECK(entity_manager_is_alive(&manager, wrapped));
#endif
}
