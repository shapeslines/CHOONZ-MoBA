#include "sim/sim_hash.h"

#include <cstring>

static const uint64_t FNV1A64_OFFSET = 14695981039346656037ULL;
static const uint64_t FNV1A64_PRIME = 1099511628211ULL;

typedef struct UnitStateView {
    mm::fix position_x;
    mm::fix position_y;
    mm::fix velocity_x;
    mm::fix velocity_y;
    int32_t health;
    uint32_t cooldown;
} UnitStateView;

static uint64_t hash_u8(uint64_t hash, uint8_t value) {
    return (hash ^ value) * FNV1A64_PRIME;
}

static uint64_t hash_u32_le(uint64_t hash, uint32_t value) {
    for (uint32_t i = 0; i < 4u; ++i)
        hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

static uint64_t hash_u64_le(uint64_t hash, uint64_t value) {
    for (uint32_t i = 0; i < 8u; ++i)
        hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

static uint32_t i32_bits(int32_t value) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool read_unit(const SimWorld* world, uint32_t unit, UnitStateView* out) {
    if (!world || !out || unit >= world->config.initial_unit_count || unit >= SIM_MAX_UNITS)
        return false;
    EntityId entity = world->unit_entities[unit];
    uint32_t transform_dense = component_pool_dense_index(&world->transforms.membership, entity);
    uint32_t velocity_dense = component_pool_dense_index(&world->velocities.membership, entity);
    uint32_t health_dense = component_pool_dense_index(&world->health.membership, entity);
    if (transform_dense == COMPONENT_POOL_INVALID_DENSE ||
        velocity_dense == COMPONENT_POOL_INVALID_DENSE ||
        health_dense == COMPONENT_POOL_INVALID_DENSE) return false;

    out->position_x = world->transforms.position_x[transform_dense];
    out->position_y = world->transforms.position_y[transform_dense];
    out->velocity_x = world->velocities.velocity_x[velocity_dense];
    out->velocity_y = world->velocities.velocity_y[velocity_dense];
    out->health = world->health.current[health_dense];
    out->cooldown = world->health.damage_cooldown[health_dense];
    return true;
}

uint64_t sim_hash_state(const SimWorld* world) {
    if (!world || !sim_validate_commands(world, nullptr)) return 0u;

    uint64_t hash = FNV1A64_OFFSET;
    hash = hash_u64_le(hash, world->tick);
    hash = hash_u32_le(hash, world->config.initial_unit_count);
    hash = hash_u64_le(hash, world->rng.state);
    hash = hash_u64_le(hash, world->rng.inc);

    for (uint32_t field = 0u; field < 6u; ++field) {
        for (uint32_t unit = 0u; unit < world->config.initial_unit_count; ++unit) {
            UnitStateView state{};
            if (!read_unit(world, unit, &state)) return 0u;
            switch (field) {
                case 0u: hash = hash_u32_le(hash, i32_bits(state.position_x)); break;
                case 1u: hash = hash_u32_le(hash, i32_bits(state.position_y)); break;
                case 2u: hash = hash_u32_le(hash, i32_bits(state.velocity_x)); break;
                case 3u: hash = hash_u32_le(hash, i32_bits(state.velocity_y)); break;
                case 4u: hash = hash_u32_le(hash, i32_bits(state.health)); break;
                default: hash = hash_u32_le(hash, state.cooldown); break;
            }
        }
    }
    return hash;
}

static void set_diff(SimStateDiff* diff, SimStateField field, uint32_t unit,
                     uint64_t expected, uint64_t actual) {
    diff->field = field;
    diff->unit_index = unit;
    diff->expected_value = expected;
    diff->actual_value = actual;
}

static uint64_t unit_field_bits(const UnitStateView& state, SimStateField field) {
    switch (field) {
        case SIM_STATE_FIELD_POSITION_X: return i32_bits(state.position_x);
        case SIM_STATE_FIELD_POSITION_Y: return i32_bits(state.position_y);
        case SIM_STATE_FIELD_VELOCITY_X: return i32_bits(state.velocity_x);
        case SIM_STATE_FIELD_VELOCITY_Y: return i32_bits(state.velocity_y);
        case SIM_STATE_FIELD_HEALTH: return i32_bits(state.health);
        case SIM_STATE_FIELD_COOLDOWN: return state.cooldown;
        default: return 0u;
    }
}

bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff) {
    if (!out_diff) return false;
    set_diff(out_diff, SIM_STATE_FIELD_NONE, SIM_STATE_DIFF_NO_UNIT, 0u, 0u);
    if (!expected || !actual || !sim_validate_commands(expected, nullptr) ||
        !sim_validate_commands(actual, nullptr)) {
        set_diff(out_diff, SIM_STATE_FIELD_INVALID, SIM_STATE_DIFF_NO_UNIT, 0u, 0u);
        return true;
    }

    if (expected->tick != actual->tick) {
        set_diff(out_diff, SIM_STATE_FIELD_TICK, SIM_STATE_DIFF_NO_UNIT,
                 expected->tick, actual->tick);
        return true;
    }
    if (expected->config.initial_unit_count != actual->config.initial_unit_count) {
        set_diff(out_diff, SIM_STATE_FIELD_UNIT_COUNT, SIM_STATE_DIFF_NO_UNIT,
                 expected->config.initial_unit_count, actual->config.initial_unit_count);
        return true;
    }
    if (expected->rng.state != actual->rng.state) {
        set_diff(out_diff, SIM_STATE_FIELD_RNG_STATE, SIM_STATE_DIFF_NO_UNIT,
                 expected->rng.state, actual->rng.state);
        return true;
    }
    if (expected->rng.inc != actual->rng.inc) {
        set_diff(out_diff, SIM_STATE_FIELD_RNG_INC, SIM_STATE_DIFF_NO_UNIT,
                 expected->rng.inc, actual->rng.inc);
        return true;
    }

    static const SimStateField fields[] = {
        SIM_STATE_FIELD_POSITION_X, SIM_STATE_FIELD_POSITION_Y,
        SIM_STATE_FIELD_VELOCITY_X, SIM_STATE_FIELD_VELOCITY_Y,
        SIM_STATE_FIELD_HEALTH, SIM_STATE_FIELD_COOLDOWN,
    };
    for (uint32_t field_index = 0u; field_index < sizeof(fields) / sizeof(fields[0]); ++field_index) {
        for (uint32_t unit = 0u; unit < expected->config.initial_unit_count; ++unit) {
            UnitStateView expected_unit{};
            UnitStateView actual_unit{};
            if (!read_unit(expected, unit, &expected_unit) || !read_unit(actual, unit, &actual_unit)) {
                set_diff(out_diff, SIM_STATE_FIELD_INVALID, SIM_STATE_DIFF_NO_UNIT, 0u, 0u);
                return true;
            }
            uint64_t expected_bits = unit_field_bits(expected_unit, fields[field_index]);
            uint64_t actual_bits = unit_field_bits(actual_unit, fields[field_index]);
            if (expected_bits != actual_bits) {
                set_diff(out_diff, fields[field_index], unit, expected_bits, actual_bits);
                return true;
            }
        }
    }
    return false;
}

const char* sim_state_field_name(SimStateField field) {
    switch (field) {
        case SIM_STATE_FIELD_NONE: return "none";
        case SIM_STATE_FIELD_INVALID: return "invalid";
        case SIM_STATE_FIELD_TICK: return "tick";
        case SIM_STATE_FIELD_UNIT_COUNT: return "unit_count";
        case SIM_STATE_FIELD_RNG_STATE: return "rng_state";
        case SIM_STATE_FIELD_RNG_INC: return "rng_inc";
        case SIM_STATE_FIELD_POSITION_X: return "position_x";
        case SIM_STATE_FIELD_POSITION_Y: return "position_y";
        case SIM_STATE_FIELD_VELOCITY_X: return "velocity_x";
        case SIM_STATE_FIELD_VELOCITY_Y: return "velocity_y";
        case SIM_STATE_FIELD_HEALTH: return "health";
        case SIM_STATE_FIELD_COOLDOWN: return "cooldown";
        default: return "invalid";
    }
}
