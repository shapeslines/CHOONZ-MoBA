#include "test.h"
#include "sim/sim.h"

#include <cstring>

static const size_t SCHEDULE_WORLD_BYTES = 8192u;

typedef struct ScheduleFixture {
    alignas(16) uint8_t storage[SCHEDULE_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} ScheduleFixture;

static bool schedule_fixture_init(ScheduleFixture* fixture,
                                  uint32_t event_capacity = 8u) {
    if (!fixture) return false;
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return sim_init(&fixture->world, &fixture->arena, 17u,
                    SimWorldConfig{8u, 4u, event_capacity});
}

static SimCommand schedule_velocity(uint16_t unit, mm::fix velocity_x) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.unit_index = unit;
    command.value_x = velocity_x;
    return command;
}

static SimCommand schedule_damage(uint16_t unit, int32_t amount) {
    SimCommand command{};
    command.kind = SIM_COMMAND_DAMAGE;
    command.unit_index = unit;
    command.amount = amount;
    return command;
}

TEST(sim_schedule, literal_order_preserves_same_tick_command_and_damage_behavior) {
    ScheduleFixture fixture{};
    CHECK(schedule_fixture_init(&fixture));
    EntityId entity = fixture.world.unit_entities[2];
    TransformView transform{};
    HealthView health{};
    CHECK(transform_pool_get(&fixture.world.transforms, entity, &transform));
    CHECK(health_pool_get(&fixture.world.health, entity, &health));
    mm::fix start_x = *transform.position_x;

    mm::pcg32 expected_rng = fixture.world.rng;
    (void)mm::pcg32_next(&expected_rng);
    SimCommand commands[] = {
        schedule_velocity(2u, mm::fix_from_int(6)),
        schedule_damage(2u, 30),
    };
    SimCommandBuffer buffer{commands, 2u};
    CHECK(sim_tick(&fixture.world, &buffer));

    CHECK(*transform.position_x ==
          start_x + mm::fix_mul(mm::fix_from_int(6), SIM_DT_FIXED));
    CHECK(*health.current == 70);
    CHECK(*health.damage_cooldown == 2u);
    CHECK(fixture.world.rng.state == expected_rng.state);
    CHECK(fixture.world.rng.inc == expected_rng.inc);
    CHECK(fixture.world.tick == 1u);
    CHECK(fixture.world.damage_events.counts[0] == 0u);
    CHECK(fixture.world.damage_events.counts[1] == 0u);
}

TEST(sim_schedule, empty_phase_buffers_swap_once_per_tick) {
    ScheduleFixture fixture{};
    CHECK(schedule_fixture_init(&fixture));
    CHECK(fixture.world.damage_events.read_index == 0u);
    CHECK(fixture.world.damage_events.write_index == 1u);
    CHECK(sim_tick(&fixture.world, nullptr));
    CHECK(fixture.world.damage_events.read_index == 1u);
    CHECK(fixture.world.damage_events.write_index == 0u);
    CHECK(sim_tick(&fixture.world, nullptr));
    CHECK(fixture.world.damage_events.read_index == 0u);
    CHECK(fixture.world.damage_events.write_index == 1u);
    CHECK(fixture.world.tick == 2u);
}

TEST(sim_schedule, damage_capacity_is_preflighted_before_any_command_mutation) {
    ScheduleFixture fixture{};
    CHECK(schedule_fixture_init(&fixture, 1u));
    SimCommand commands[] = {
        schedule_velocity(0u, mm::fix_from_int(9)),
        schedule_damage(0u, 1),
        schedule_damage(1u, 2),
    };
    SimCommandBuffer buffer{commands, 3u};
    SimWorld before = fixture.world;
    uint8_t bytes_before[SCHEDULE_WORLD_BYTES];
    std::memcpy(bytes_before, fixture.storage, sizeof(bytes_before));

    CHECK(!sim_validate_commands(&fixture.world, &buffer));
    CHECK(!sim_tick(&fixture.world, &buffer));
    CHECK(std::memcmp(&fixture.world, &before, sizeof(before)) == 0);
    CHECK(std::memcmp(fixture.storage, bytes_before, sizeof(bytes_before)) == 0);
}

TEST(sim_schedule, deferred_destroy_commits_after_same_tick_systems) {
    ScheduleFixture fixture{};
    CHECK(schedule_fixture_init(&fixture));
    EntityId doomed = fixture.world.unit_entities[1];
    CHECK(sim_destroy_deferred(&fixture.world, doomed));

    SimCommand commands[] = {
        schedule_velocity(1u, mm::fix_from_int(3)),
        schedule_damage(1u, 10),
    };
    SimCommandBuffer buffer{commands, 2u};
    CHECK(sim_tick(&fixture.world, &buffer));
    CHECK(!entity_manager_is_alive(&fixture.world.entities, doomed));
    CHECK(fixture.world.unit_entities[1].h == HANDLE_NULL);
    CHECK(fixture.world.pending_destroy_count == 0u);
}
