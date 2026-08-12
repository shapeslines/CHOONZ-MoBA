#include "test.h"
#include "sim/sim_hash.h"

#include <cstring>

static void check_array_hash_sensitivity(const SimWorld& base, SimStateField field) {
    uint64_t baseline = sim_hash_state(&base);
    for (uint32_t i = 0; i < base.unit_count; ++i) {
        SimWorld changed = base;
        switch (field) {
            case SIM_STATE_FIELD_POSITION_X: changed.position_x[i] ^= 1; break;
            case SIM_STATE_FIELD_POSITION_Y: changed.position_y[i] ^= 1; break;
            case SIM_STATE_FIELD_VELOCITY_X: changed.velocity_x[i] ^= 1; break;
            case SIM_STATE_FIELD_VELOCITY_Y: changed.velocity_y[i] ^= 1; break;
            case SIM_STATE_FIELD_HEALTH: changed.health[i] ^= 1; break;
            case SIM_STATE_FIELD_COOLDOWN: changed.cooldown[i] ^= 1u; break;
            default: break;
        }
        CHECK(sim_hash_state(&changed) != baseline);
    }
}

TEST(sim, hash_covers_every_authoritative_field) {
    SimWorld base;
    sim_init(&base, 1234);
    uint64_t baseline = sim_hash_state(&base);
    CHECK(baseline == 0x0f08f2cd7e26d1b2ULL); // pins field order + LE encoding

    SimWorld changed = base;
    ++changed.tick; CHECK(sim_hash_state(&changed) != baseline);
    changed = base; --changed.unit_count; CHECK(sim_hash_state(&changed) != baseline);
    changed = base; changed.rng.state ^= 1u; CHECK(sim_hash_state(&changed) != baseline);
    changed = base; changed.rng.inc ^= 2u; CHECK(sim_hash_state(&changed) != baseline);

    check_array_hash_sensitivity(base, SIM_STATE_FIELD_POSITION_X);
    check_array_hash_sensitivity(base, SIM_STATE_FIELD_POSITION_Y);
    check_array_hash_sensitivity(base, SIM_STATE_FIELD_VELOCITY_X);
    check_array_hash_sensitivity(base, SIM_STATE_FIELD_VELOCITY_Y);
    check_array_hash_sensitivity(base, SIM_STATE_FIELD_HEALTH);
    check_array_hash_sensitivity(base, SIM_STATE_FIELD_COOLDOWN);
}

TEST(sim, hash_excludes_padding_and_unused_capacity) {
    SimWorld a, b;
    std::memset(&a, 0xaa, sizeof(a));
    std::memset(&b, 0x55, sizeof(b));
    a.tick = b.tick = 9;
    a.unit_count = b.unit_count = 1;
    a.rng.state = b.rng.state = 123;
    a.rng.inc = b.rng.inc = 7;
    a.position_x[0] = b.position_x[0] = 10;
    a.position_y[0] = b.position_y[0] = 20;
    a.velocity_x[0] = b.velocity_x[0] = 30;
    a.velocity_y[0] = b.velocity_y[0] = 40;
    a.health[0] = b.health[0] = 50;
    a.cooldown[0] = b.cooldown[0] = 60;
    CHECK(std::memcmp(&a, &b, sizeof(a)) != 0);
    CHECK(sim_hash_state(&a) == sim_hash_state(&b));
}

TEST(sim, diff_reports_first_field_and_unit) {
    SimWorld expected, actual;
    sim_init(&expected, 77);
    actual = expected;
    actual.position_x[7] ^= 1;

    SimStateDiff diff{};
    CHECK(sim_diff_state(&expected, &actual, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_POSITION_X);
    CHECK(diff.unit_index == 7);
    CHECK(std::strcmp(sim_state_field_name(diff.field), "position_x") == 0);
    CHECK(diff.expected_value == static_cast<uint32_t>(expected.position_x[7]));
    CHECK(diff.actual_value == static_cast<uint32_t>(actual.position_x[7]));

    actual.tick = expected.tick + 1;
    CHECK(sim_diff_state(&expected, &actual, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_TICK);
    CHECK(diff.unit_index == SIM_STATE_DIFF_NO_UNIT);

    actual = expected;
    CHECK(!sim_diff_state(&expected, &actual, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);
}

TEST(sim, invalid_hash_and_diff_fail_closed) {
    SimWorld invalid;
    sim_init(&invalid, 1);
    invalid.unit_count = SIM_MAX_UNITS + 1;
    CHECK(sim_hash_state(nullptr) == 0);
    CHECK(sim_hash_state(&invalid) == 0);
    SimStateDiff diff{};
    CHECK(sim_diff_state(&invalid, &invalid, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_INVALID);
}
