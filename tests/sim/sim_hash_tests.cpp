#include "test.h"
#include "sim/sim_hash.h"

#include <cstring>

static const size_t HASH_WORLD_BYTES = 8192u;

typedef struct HashWorldFixture {
    alignas(16) uint8_t storage[HASH_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} HashWorldFixture;

static bool hash_world_init(HashWorldFixture* fixture, uint64_t seed,
                            uint32_t initial_units = SIM_MAX_UNITS) {
    if (!fixture) return false;
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return sim_init(&fixture->world, &fixture->arena, seed,
                    SimWorldConfig{SIM_MAX_UNITS, initial_units});
}

static bool mutate_unit_field(SimWorld* world, uint32_t unit, SimStateField field) {
    EntityId entity = world->unit_entities[unit];
    TransformView transform{};
    VelocityView velocity{};
    HealthView health{};
    if (!transform_pool_get(&world->transforms, entity, &transform) ||
        !velocity_pool_get(&world->velocities, entity, &velocity) ||
        !health_pool_get(&world->health, entity, &health)) return false;
    switch (field) {
        case SIM_STATE_FIELD_POSITION_X: *transform.position_x ^= 1; break;
        case SIM_STATE_FIELD_POSITION_Y: *transform.position_y ^= 1; break;
        case SIM_STATE_FIELD_VELOCITY_X: *velocity.velocity_x ^= 1; break;
        case SIM_STATE_FIELD_VELOCITY_Y: *velocity.velocity_y ^= 1; break;
        case SIM_STATE_FIELD_HEALTH: *health.current ^= 1; break;
        case SIM_STATE_FIELD_COOLDOWN: *health.damage_cooldown ^= 1u; break;
        default: return false;
    }
    return true;
}

static void check_array_hash_sensitivity(uint64_t baseline, SimStateField field) {
    for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit) {
        HashWorldFixture changed{};
        CHECK(hash_world_init(&changed, 1234u));
        CHECK(mutate_unit_field(&changed.world, unit, field));
        CHECK(sim_hash_state(&changed.world) != baseline);
    }
}

TEST(sim, hash_covers_current_authoritative_unit_values) {
    HashWorldFixture base{};
    CHECK(hash_world_init(&base, 1234u));
    uint64_t baseline = sim_hash_state(&base.world);
    CHECK(baseline == 0x0f08f2cd7e26d1b2ULL); // semantic M3.0 order during S3 migration

    HashWorldFixture changed{};
    CHECK(hash_world_init(&changed, 1234u));
    ++changed.world.tick;
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(hash_world_init(&changed, 1234u));
    --changed.world.config.initial_unit_count;
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(hash_world_init(&changed, 1234u));
    changed.world.rng.state ^= 1u;
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(hash_world_init(&changed, 1234u));
    changed.world.rng.inc ^= 2u;
    CHECK(sim_hash_state(&changed.world) != baseline);

    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_POSITION_X);
    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_POSITION_Y);
    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_VELOCITY_X);
    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_VELOCITY_Y);
    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_HEALTH);
    check_array_hash_sensitivity(baseline, SIM_STATE_FIELD_COOLDOWN);
}

TEST(sim, hash_excludes_pointers_and_unused_component_values) {
    HashWorldFixture a{}, b{};
    CHECK(hash_world_init(&a, 77u, 1u));
    CHECK(hash_world_init(&b, 77u, 1u));
    CHECK(std::memcmp(&a.world, &b.world, sizeof(a.world)) != 0);
    CHECK(sim_hash_state(&a.world) == sim_hash_state(&b.world));

    b.world.transforms.position_x[7] = 111;
    b.world.transforms.position_y[7] = 222;
    b.world.velocities.velocity_x[7] = 333;
    b.world.velocities.velocity_y[7] = 444;
    b.world.health.current[7] = 55;
    b.world.health.maximum[7] = 66;
    b.world.health.damage_cooldown[7] = 77u;
    CHECK(sim_hash_state(&a.world) == sim_hash_state(&b.world));
}

TEST(sim, diff_reports_first_field_and_unit) {
    HashWorldFixture expected{}, actual{};
    CHECK(hash_world_init(&expected, 77u));
    CHECK(hash_world_init(&actual, 77u));
    CHECK(mutate_unit_field(&actual.world, 7u, SIM_STATE_FIELD_POSITION_X));

    SimStateDiff diff{};
    CHECK(sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_POSITION_X);
    CHECK(diff.unit_index == 7u);
    CHECK(std::strcmp(sim_state_field_name(diff.field), "position_x") == 0);
    CHECK(diff.expected_value != diff.actual_value);

    actual.world.tick = expected.world.tick + 1u;
    CHECK(sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_TICK);
    CHECK(diff.unit_index == SIM_STATE_DIFF_NO_UNIT);

    CHECK(hash_world_init(&actual, 77u));
    CHECK(!sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);
}

TEST(sim, invalid_hash_and_diff_fail_closed) {
    HashWorldFixture invalid{};
    CHECK(hash_world_init(&invalid, 1u));
    invalid.world.config.initial_unit_count = SIM_MAX_UNITS + 1u;
    CHECK(sim_hash_state(nullptr) == 0u);
    CHECK(sim_hash_state(&invalid.world) == 0u);
    SimStateDiff diff{};
    CHECK(sim_diff_state(&invalid.world, &invalid.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_INVALID);
}
