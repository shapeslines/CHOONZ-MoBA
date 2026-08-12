#include "test.h"
#include "sim/sim.h"

#include <cstring>

static const size_t DESTROY_WORLD_BYTES = 4096u;

typedef struct DestroyFixture {
    alignas(16) uint8_t storage[DESTROY_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} DestroyFixture;

static bool destroy_fixture_init(DestroyFixture* fixture,
                                 uint32_t max_entities = 8u,
                                 uint32_t initial_units = 4u) {
    if (!fixture) return false;
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return sim_init(&fixture->world, &fixture->arena, 123u,
                    SimWorldConfig{max_entities, initial_units, 8u});
}

static SimCommand destroy_test_velocity(uint16_t unit, mm::fix x) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.unit_index = unit;
    command.value_x = x;
    return command;
}

TEST(sim_destroy, invalid_duplicate_and_full_requests_are_atomic) {
    DestroyFixture fixture{};
    CHECK(destroy_fixture_init(&fixture));
    EntityId entity = fixture.world.unit_entities[1];
    CHECK(sim_destroy_deferred(&fixture.world, entity));

    SimWorld before = fixture.world;
    uint8_t bytes_before[DESTROY_WORLD_BYTES];
    std::memcpy(bytes_before, fixture.storage, sizeof(fixture.storage));
    CHECK(!sim_destroy_deferred(&fixture.world, entity));
    CHECK(!sim_destroy_deferred(&fixture.world, EntityId{HANDLE_NULL}));
    CHECK(std::memcmp(&fixture.world, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(fixture.storage, bytes_before, sizeof(bytes_before)) == 0);

    EntityId extra = entity_manager_create(&fixture.world.entities);
    CHECK(extra.h != HANDLE_NULL);
    CHECK(entity_manager_release(&fixture.world.entities, extra));
    before = fixture.world;
    std::memcpy(bytes_before, fixture.storage, sizeof(fixture.storage));
    CHECK(!sim_destroy_deferred(&fixture.world, extra));
    CHECK(std::memcmp(&fixture.world, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(fixture.storage, bytes_before, sizeof(bytes_before)) == 0);

    DestroyFixture full{};
    CHECK(destroy_fixture_init(&full, 4u, 4u));
    for (uint32_t i = 0u; i < 4u; ++i)
        CHECK(sim_destroy_deferred(&full.world, full.world.unit_entities[i]));
    SimWorld full_before = full.world;
    uint8_t full_bytes_before[DESTROY_WORLD_BYTES];
    std::memcpy(full_bytes_before, full.storage, sizeof(full.storage));
    CHECK(!sim_destroy_deferred(&full.world, full.world.unit_entities[0]));
    CHECK(std::memcmp(&full.world, &full_before, sizeof(full_before)) == 0);
    CHECK(std::memcmp(full.storage, full_bytes_before, sizeof(full_bytes_before)) == 0);
}

TEST(sim_destroy, entity_is_usable_this_tick_and_gone_at_the_boundary) {
    DestroyFixture fixture{};
    CHECK(destroy_fixture_init(&fixture));
    EntityId doomed = fixture.world.unit_entities[1];
    CHECK(sim_destroy_deferred(&fixture.world, doomed));

    SimCommand command = destroy_test_velocity(1u, mm::fix_from_int(3));
    SimCommandBuffer commands{&command, 1u};
    CHECK(sim_validate_commands(&fixture.world, &commands));
    CHECK(entity_manager_is_alive(&fixture.world.entities, doomed));
    CHECK(transform_pool_has(&fixture.world.transforms, doomed));
    CHECK(velocity_pool_has(&fixture.world.velocities, doomed));
    CHECK(health_pool_has(&fixture.world.health, doomed));
    CHECK(sim_tick(&fixture.world, &commands));

    CHECK(fixture.world.tick == 1u);
    CHECK(fixture.world.pending_destroy_count == 0u);
    CHECK(fixture.world.unit_entities[1].h == HANDLE_NULL);
    CHECK(!entity_manager_is_alive(&fixture.world.entities, doomed));
    CHECK(!transform_pool_has(&fixture.world.transforms, doomed));
    CHECK(!velocity_pool_has(&fixture.world.velocities, doomed));
    CHECK(!health_pool_has(&fixture.world.health, doomed));
    CHECK(fixture.world.entities.live_count == 3u);

    EntityId reused = entity_manager_create(&fixture.world.entities);
    CHECK(handle_index(reused.h) == 1u);
    CHECK(handle_gen(reused.h) == 2u);
    CHECK(entity_manager_is_alive(&fixture.world.entities, reused));
    CHECK(!entity_manager_is_alive(&fixture.world.entities, doomed));
    CHECK(!sim_destroy_deferred(&fixture.world, doomed));
}

TEST(sim_destroy, recorded_request_order_defines_lifo_slot_reuse) {
    DestroyFixture fixture{};
    CHECK(destroy_fixture_init(&fixture));
    EntityId first = fixture.world.unit_entities[0];
    EntityId second = fixture.world.unit_entities[2];
    CHECK(sim_destroy_deferred(&fixture.world, first));
    CHECK(sim_destroy_deferred(&fixture.world, second));
    CHECK(sim_tick(&fixture.world, nullptr));

    EntityId recycled_second = entity_manager_create(&fixture.world.entities);
    EntityId recycled_first = entity_manager_create(&fixture.world.entities);
    CHECK(recycled_second.h == handle_make(2u, 2u));
    CHECK(recycled_first.h == handle_make(0u, 2u));
    CHECK(fixture.world.unit_entities[0].h == HANDLE_NULL);
    CHECK(fixture.world.unit_entities[2].h == HANDLE_NULL);
}

TEST(sim_destroy, rejected_tick_preserves_the_pending_queue) {
    DestroyFixture fixture{};
    CHECK(destroy_fixture_init(&fixture));
    EntityId doomed = fixture.world.unit_entities[3];
    CHECK(sim_destroy_deferred(&fixture.world, doomed));
    SimCommand malformed = destroy_test_velocity(3u, 1);
    malformed.kind = 0xff;
    SimCommandBuffer commands{&malformed, 1u};

    SimWorld before = fixture.world;
    uint8_t bytes_before[DESTROY_WORLD_BYTES];
    std::memcpy(bytes_before, fixture.storage, sizeof(fixture.storage));
    CHECK(!sim_tick(&fixture.world, &commands));
    CHECK(std::memcmp(&fixture.world, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(fixture.storage, bytes_before, sizeof(bytes_before)) == 0);
    CHECK(fixture.world.pending_destroy_count == 1u);
    CHECK(fixture.world.pending_destroy[0].h == doomed.h);
    CHECK(entity_manager_is_alive(&fixture.world.entities, doomed));
}

TEST(sim_destroy, componentless_unmapped_entities_destroy_cleanly) {
    DestroyFixture fixture{};
    CHECK(destroy_fixture_init(&fixture));
    EntityId entity = entity_manager_create(&fixture.world.entities);
    CHECK(entity.h != HANDLE_NULL);
    CHECK(!transform_pool_has(&fixture.world.transforms, entity));
    CHECK(sim_destroy_deferred(&fixture.world, entity));
    CHECK(sim_tick(&fixture.world, nullptr));
    CHECK(!entity_manager_is_alive(&fixture.world.entities, entity));
}
