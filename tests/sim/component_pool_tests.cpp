#include "test.h"
#include "sim/component_pool.h"

#include <cstring>

static EntityId pool_entity(uint32_t index, uint32_t generation = 1u) {
    return EntityId{handle_make(index, generation)};
}

static void check_bidirectional_membership(const ComponentPool* pool,
                                           const bool* active,
                                           const uint16_t* generations,
                                           uint32_t entity_count) {
    uint32_t observed = 0u;
    for (uint32_t i = 0; i < entity_count; ++i) {
        EntityId entity = pool_entity(i, generations[i]);
        if (active[i]) {
            ++observed;
            CHECK(component_pool_has(pool, entity));
            uint32_t dense = component_pool_dense_index(pool, entity);
            CHECK(dense < pool->count);
            CHECK(pool->dense_entities[dense].h == entity.h);
            CHECK(pool->sparse[i] == dense + 1u);
        } else {
            CHECK(!component_pool_has(pool, entity));
            CHECK(pool->sparse[i] == 0u);
        }
    }
    CHECK(observed == pool->count);
    for (uint32_t dense = 0; dense < pool->count; ++dense) {
        uint32_t index = handle_index(pool->dense_entities[dense].h);
        CHECK(index < entity_count);
        CHECK(active[index]);
        CHECK(pool->sparse[index] == dense + 1u);
    }
}

TEST(sim_component_pool, initialization_is_arena_backed_and_atomic) {
    alignas(16) uint8_t storage[256]{};
    Arena arena;
    arena_init_fixed(&arena, storage, component_pool_memory_required(8u, 4u));
    ComponentPool pool{};
    CHECK(component_pool_init(&pool, &arena, 8u, 4u));
    CHECK(pool.entity_capacity == 8u);
    CHECK(pool.capacity == 4u);
    CHECK(pool.count == 0u);
    CHECK(pool.ordered_count == 0u);
    CHECK(pool.ordered_dirty == 1u);
    CHECK(arena.offset <= component_pool_memory_required(8u, 4u));
    for (uint32_t i = 0; i < 8u; ++i) CHECK(pool.sparse[i] == 0u);
    for (uint32_t i = 0; i < 4u; ++i) {
        CHECK(pool.dense_entities[i].h == HANDLE_NULL);
        CHECK(pool.ordered_entities[i].h == HANDLE_NULL);
    }

    alignas(16) uint8_t short_storage[8]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, sizeof(short_storage));
    ComponentPool untouched{};
    untouched.capacity = 99u;
    ComponentPool before = untouched;
    CHECK(!component_pool_init(&untouched, &short_arena, 8u, 4u));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
    CHECK(short_arena.offset == 0u);
    CHECK(!component_pool_init(&untouched, &short_arena, 0u, 0u));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
    CHECK(short_arena.offset == 0u);
}

TEST(sim_component_pool, failed_adds_and_removes_are_mutation_free) {
    alignas(16) uint8_t storage[256]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    ComponentPool pool{};
    CHECK(component_pool_init(&pool, &arena, 8u, 3u));
    CHECK(component_pool_add(&pool, pool_entity(0u), nullptr));
    CHECK(component_pool_add(&pool, pool_entity(1u), nullptr));
    CHECK(component_pool_add(&pool, pool_entity(2u), nullptr));

    ComponentPool before = pool;
    uint8_t bytes_before[256];
    std::memcpy(bytes_before, storage, sizeof(storage));
    uint32_t dense_output = 77u;
    ComponentPoolRemoveResult remove_output{88u, 99u, pool_entity(7u)};

    CHECK(!component_pool_add(&pool, pool_entity(1u), &dense_output));
    CHECK(dense_output == 77u);
    CHECK(!component_pool_add(&pool, pool_entity(1u, 2u), &dense_output));
    CHECK(!component_pool_add(&pool, pool_entity(3u), &dense_output));
    CHECK(!component_pool_add(&pool, EntityId{HANDLE_NULL}, &dense_output));
    CHECK(!component_pool_remove(&pool, pool_entity(1u, 2u), &remove_output));
    CHECK(!component_pool_remove(&pool, pool_entity(7u), &remove_output));
    CHECK(remove_output.removed_dense == 88u);
    CHECK(remove_output.moved_from_dense == 99u);
    CHECK(remove_output.moved_entity.h == pool_entity(7u).h);
    CHECK(std::memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);
}

TEST(sim_component_pool, swap_remove_repairs_first_middle_and_last_positions) {
    alignas(16) uint8_t storage[512]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    ComponentPool pool{};
    CHECK(component_pool_init(&pool, &arena, 8u, 5u));
    for (uint32_t i = 0; i < 5u; ++i) CHECK(component_pool_add(&pool, pool_entity(i), nullptr));

    ComponentPoolRemoveResult middle{};
    CHECK(component_pool_remove(&pool, pool_entity(2u), &middle));
    CHECK(middle.removed_dense == 2u);
    CHECK(middle.moved_from_dense == 4u);
    CHECK(middle.moved_entity.h == pool_entity(4u).h);
    CHECK(component_pool_dense_index(&pool, pool_entity(4u)) == 2u);
    CHECK(pool.sparse[2] == 0u);

    ComponentPoolRemoveResult first{};
    CHECK(component_pool_remove(&pool, pool_entity(0u), &first));
    CHECK(first.removed_dense == 0u);
    CHECK(first.moved_from_dense == 3u);
    CHECK(first.moved_entity.h == pool_entity(3u).h);
    CHECK(component_pool_dense_index(&pool, pool_entity(3u)) == 0u);
    CHECK(pool.sparse[0] == 0u);

    ComponentPoolRemoveResult last{};
    CHECK(component_pool_remove(&pool, pool_entity(4u), &last));
    CHECK(last.removed_dense == 2u);
    CHECK(last.moved_from_dense == 2u);
    CHECK(last.moved_entity.h == HANDLE_NULL);
    CHECK(pool.sparse[4] == 0u);
    CHECK(pool.count == 2u);
}

TEST(sim_component_pool, long_churn_preserves_both_mapping_directions) {
    alignas(16) uint8_t storage[1024]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    ComponentPool pool{};
    CHECK(component_pool_init(&pool, &arena, 32u, 32u));

    bool active[32]{};
    uint16_t generations[32];
    for (uint32_t i = 0; i < 32u; ++i) generations[i] = 1u;

    for (uint32_t step = 0; step < 1024u; ++step) {
        uint32_t index = (step * 17u + 5u) % 32u;
        EntityId entity = pool_entity(index, generations[index]);
        if (active[index]) {
            CHECK(component_pool_remove(&pool, entity, nullptr));
            active[index] = false;
            ++generations[index];
        } else {
            CHECK(component_pool_add(&pool, entity, nullptr));
            active[index] = true;
        }
        check_bidirectional_membership(&pool, active, generations, 32u);
    }
}

TEST(sim_component_pool, ordered_view_is_unique_ascending_and_dense_order_independent) {
    alignas(16) uint8_t storage_a[512]{};
    alignas(16) uint8_t storage_b[512]{};
    Arena arena_a, arena_b;
    arena_init_fixed(&arena_a, storage_a, sizeof(storage_a));
    arena_init_fixed(&arena_b, storage_b, sizeof(storage_b));
    ComponentPool a{}, b{};
    CHECK(component_pool_init(&a, &arena_a, 8u, 8u));
    CHECK(component_pool_init(&b, &arena_b, 8u, 8u));

    const uint32_t order_a[] = {6u, 1u, 4u, 2u};
    const uint32_t order_b[] = {2u, 4u, 6u, 1u};
    for (uint32_t i = 0u; i < 4u; ++i) {
        CHECK(component_pool_add(&a, pool_entity(order_a[i]), nullptr));
        CHECK(component_pool_add(&b, pool_entity(order_b[i]), nullptr));
    }
    CHECK(a.dense_entities[0].h != b.dense_entities[0].h);

    ComponentPoolOrderedView view_a{}, view_b{};
    CHECK(component_pool_ordered_view(&a, &view_a));
    CHECK(component_pool_ordered_view(&b, &view_b));
    CHECK(view_a.count == 4u);
    CHECK(view_b.count == view_a.count);
    const uint32_t expected[] = {1u, 2u, 4u, 6u};
    for (uint32_t i = 0u; i < view_a.count; ++i) {
        CHECK(handle_index(view_a.entities[i].h) == expected[i]);
        CHECK(view_a.entities[i].h == view_b.entities[i].h);
    }
    CHECK(a.ordered_dirty == 0u);
    CHECK(b.ordered_dirty == 0u);
}

TEST(sim_component_pool, ordered_view_rebuilds_only_after_successful_membership_churn) {
    alignas(16) uint8_t storage[512]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    ComponentPool pool{};
    CHECK(component_pool_init(&pool, &arena, 8u, 8u));
    CHECK(component_pool_add(&pool, pool_entity(0u), nullptr));
    CHECK(component_pool_add(&pool, pool_entity(3u), nullptr));
    CHECK(component_pool_add(&pool, pool_entity(7u), nullptr));

    ComponentPoolOrderedView view{};
    CHECK(component_pool_ordered_view(&pool, &view));
    CHECK(view.count == 3u);
    CHECK(pool.ordered_dirty == 0u);
    const EntityId* stable_cache = view.entities;
    CHECK(component_pool_ordered_view(&pool, &view));
    CHECK(view.entities == stable_cache);
    CHECK(pool.ordered_dirty == 0u);

    ComponentPool before = pool;
    uint8_t bytes_before[512];
    std::memcpy(bytes_before, storage, sizeof(storage));
    CHECK(!component_pool_add(&pool, pool_entity(3u), nullptr));
    CHECK(!component_pool_remove(&pool, pool_entity(5u), nullptr));
    CHECK(std::memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    CHECK(component_pool_remove(&pool, pool_entity(3u), nullptr));
    CHECK(pool.ordered_dirty == 1u);
    CHECK(component_pool_add(&pool, pool_entity(2u), nullptr));
    CHECK(component_pool_add(&pool, pool_entity(5u), nullptr));
    CHECK(component_pool_ordered_view(&pool, &view));
    CHECK(view.count == 4u);
    const uint32_t expected[] = {0u, 2u, 5u, 7u};
    for (uint32_t i = 0u; i < view.count; ++i)
        CHECK(handle_index(view.entities[i].h) == expected[i]);
    CHECK(pool.ordered_dirty == 0u);
}
