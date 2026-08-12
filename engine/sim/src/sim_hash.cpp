#include "sim/sim_hash.h"

#include <cstring>

static const uint64_t FNV1A64_OFFSET = 14695981039346656037ULL;
static const uint64_t FNV1A64_PRIME = 1099511628211ULL;

static uint64_t hash_u8(uint64_t hash, uint8_t value) {
    return (hash ^ value) * FNV1A64_PRIME;
}

static uint64_t hash_u32_le(uint64_t hash, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8)));
    return hash;
}

static uint64_t hash_u64_le(uint64_t hash, uint64_t value) {
    for (uint32_t i = 0; i < 8; ++i) hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8)));
    return hash;
}

static uint32_t i32_bits(int32_t value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint64_t sim_hash_state(const SimWorld* world) {
    if (!world || world->unit_count > SIM_MAX_UNITS) return 0;

    uint64_t hash = FNV1A64_OFFSET;
    hash = hash_u64_le(hash, world->tick);
    hash = hash_u32_le(hash, world->unit_count);
    hash = hash_u64_le(hash, world->rng.state);
    hash = hash_u64_le(hash, world->rng.inc);

    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, i32_bits(world->position_x[i]));
    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, i32_bits(world->position_y[i]));
    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, i32_bits(world->velocity_x[i]));
    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, i32_bits(world->velocity_y[i]));
    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, i32_bits(world->health[i]));
    for (uint32_t i = 0; i < world->unit_count; ++i) hash = hash_u32_le(hash, world->cooldown[i]);
    return hash;
}

static void set_diff(SimStateDiff* diff, SimStateField field, uint32_t unit,
                     uint64_t expected, uint64_t actual) {
    diff->field = field;
    diff->unit_index = unit;
    diff->expected_value = expected;
    diff->actual_value = actual;
}

bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff) {
    if (!out_diff) return false;
    set_diff(out_diff, SIM_STATE_FIELD_NONE, SIM_STATE_DIFF_NO_UNIT, 0, 0);
    if (!expected || !actual || expected->unit_count > SIM_MAX_UNITS || actual->unit_count > SIM_MAX_UNITS) {
        set_diff(out_diff, SIM_STATE_FIELD_INVALID, SIM_STATE_DIFF_NO_UNIT, 0, 0);
        return true;
    }

    if (expected->tick != actual->tick) {
        set_diff(out_diff, SIM_STATE_FIELD_TICK, SIM_STATE_DIFF_NO_UNIT, expected->tick, actual->tick);
        return true;
    }
    if (expected->unit_count != actual->unit_count) {
        set_diff(out_diff, SIM_STATE_FIELD_UNIT_COUNT, SIM_STATE_DIFF_NO_UNIT,
                 expected->unit_count, actual->unit_count);
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

#define DIFF_I32_ARRAY(member, field_name) do {                                      \
    for (uint32_t i = 0; i < expected->unit_count; ++i) {                            \
        if (expected->member[i] != actual->member[i]) {                              \
            set_diff(out_diff, field_name, i, i32_bits(expected->member[i]),         \
                     i32_bits(actual->member[i]));                                    \
            return true;                                                             \
        }                                                                             \
    }                                                                                 \
} while (0)

#define DIFF_U32_ARRAY(member, field_name) do {                                      \
    for (uint32_t i = 0; i < expected->unit_count; ++i) {                            \
        if (expected->member[i] != actual->member[i]) {                              \
            set_diff(out_diff, field_name, i, expected->member[i], actual->member[i]); \
            return true;                                                             \
        }                                                                             \
    }                                                                                 \
} while (0)

    DIFF_I32_ARRAY(position_x, SIM_STATE_FIELD_POSITION_X);
    DIFF_I32_ARRAY(position_y, SIM_STATE_FIELD_POSITION_Y);
    DIFF_I32_ARRAY(velocity_x, SIM_STATE_FIELD_VELOCITY_X);
    DIFF_I32_ARRAY(velocity_y, SIM_STATE_FIELD_VELOCITY_Y);
    DIFF_I32_ARRAY(health, SIM_STATE_FIELD_HEALTH);
    DIFF_U32_ARRAY(cooldown, SIM_STATE_FIELD_COOLDOWN);

#undef DIFF_I32_ARRAY
#undef DIFF_U32_ARRAY
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
