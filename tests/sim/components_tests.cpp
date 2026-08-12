#include "test.h"
#include "sim/components.h"

#include <cstring>

static EntityId component_entity(uint32_t index, uint32_t generation = 1u) {
    return EntityId{handle_make(index, generation)};
}

TEST(sim_components, typed_values_round_trip_through_pointer_views) {
    alignas(16) uint8_t storage[2048]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    TransformPool transforms{};
    VelocityPool velocities{};
    HealthPool health{};
    CHECK(transform_pool_init(&transforms, &arena, 8u, 4u));
    CHECK(velocity_pool_init(&velocities, &arena, 8u, 4u));
    CHECK(health_pool_init(&health, &arena, 8u, 4u));

    EntityId entity = component_entity(3u);
    CHECK(transform_pool_add(&transforms, entity, 11, -22, 33));
    CHECK(velocity_pool_add(&velocities, entity, -44, 55));
    CHECK(health_pool_add(&health, entity, 70, 100, 6u));

    TransformView transform{};
    VelocityView velocity{};
    HealthView health_view{};
    CHECK(transform_pool_get(&transforms, entity, &transform));
    CHECK(velocity_pool_get(&velocities, entity, &velocity));
    CHECK(health_pool_get(&health, entity, &health_view));
    CHECK(*transform.position_x == 11);
    CHECK(*transform.position_y == -22);
    CHECK(*transform.facing == 33);
    CHECK(*velocity.velocity_x == -44);
    CHECK(*velocity.velocity_y == 55);
    CHECK(*health_view.current == 70);
    CHECK(*health_view.maximum == 100);
    CHECK(*health_view.damage_cooldown == 6u);

    *transform.position_x = 101;
    *velocity.velocity_y = 202;
    *health_view.current = 303;
    CHECK(transforms.position_x[0] == 101);
    CHECK(velocities.velocity_y[0] == 202);
    CHECK(health.current[0] == 303);
}

TEST(sim_components, typed_failures_are_atomic) {
    alignas(16) uint8_t storage[512]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    HealthPool pool{};
    CHECK(health_pool_init(&pool, &arena, 4u, 2u));
    CHECK(health_pool_add(&pool, component_entity(0u), 90, 100, 1u));
    CHECK(health_pool_add(&pool, component_entity(1u), 80, 100, 2u));

    HealthPool before = pool;
    uint8_t bytes_before[512];
    std::memcpy(bytes_before, storage, sizeof(storage));
    HealthView untouched{reinterpret_cast<int32_t*>(1), reinterpret_cast<int32_t*>(2),
                         reinterpret_cast<uint32_t*>(3)};
    CHECK(!health_pool_add(&pool, component_entity(1u), 1, 2, 3u));
    CHECK(!health_pool_add(&pool, component_entity(2u), 1, 2, 3u));
    CHECK(!health_pool_add(&pool, component_entity(3u), 4, 3, 0u));
    CHECK(!health_pool_remove(&pool, component_entity(0u, 2u)));
    CHECK(!health_pool_get(&pool, component_entity(0u, 2u), &untouched));
    CHECK(untouched.current == reinterpret_cast<int32_t*>(1));
    CHECK(untouched.maximum == reinterpret_cast<int32_t*>(2));
    CHECK(untouched.damage_cooldown == reinterpret_cast<uint32_t*>(3));
    CHECK(std::memcmp(&pool, &before, sizeof(pool)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    alignas(16) uint8_t short_storage[8]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, sizeof(short_storage));
    TransformPool untouched_pool{};
    untouched_pool.membership.capacity = 99u;
    TransformPool untouched_before = untouched_pool;
    CHECK(!transform_pool_init(&untouched_pool, &short_arena, 4u, 4u));
    CHECK(std::memcmp(&untouched_pool, &untouched_before, sizeof(untouched_pool)) == 0);
    CHECK(short_arena.offset == 0u);
}

TEST(sim_components, swap_remove_repairs_all_soa_arrays) {
    alignas(16) uint8_t storage[1024]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    TransformPool pool{};
    CHECK(transform_pool_init(&pool, &arena, 8u, 4u));
    for (uint32_t i = 0; i < 4u; ++i) {
        CHECK(transform_pool_add(&pool, component_entity(i),
                                 static_cast<mm::fix>(10u + i),
                                 static_cast<mm::fix>(20u + i),
                                 static_cast<mm::fix>(30u + i)));
    }

    CHECK(transform_pool_remove(&pool, component_entity(1u)));
    TransformView moved{};
    CHECK(transform_pool_get(&pool, component_entity(3u), &moved));
    CHECK(*moved.position_x == 13);
    CHECK(*moved.position_y == 23);
    CHECK(*moved.facing == 33);
    CHECK(pool.membership.sparse[1] == 0u);
    CHECK(pool.position_x[3] == 0);
    CHECK(pool.position_y[3] == 0);
    CHECK(pool.facing[3] == 0);

    CHECK(transform_pool_remove(&pool, component_entity(2u)));
    CHECK(pool.membership.count == 2u);
    CHECK(transform_pool_has(&pool, component_entity(0u)));
    CHECK(transform_pool_has(&pool, component_entity(3u)));
}
