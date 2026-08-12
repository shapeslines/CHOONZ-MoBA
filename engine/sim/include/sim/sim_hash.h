#pragma once

#include <stdint.h>

#include "sim/sim.h"

typedef enum SimStateField : uint8_t {
    SIM_STATE_FIELD_NONE = 0,
    SIM_STATE_FIELD_INVALID,
    SIM_STATE_FIELD_TICK,
    SIM_STATE_FIELD_UNIT_COUNT,
    SIM_STATE_FIELD_RNG_STATE,
    SIM_STATE_FIELD_RNG_INC,
    SIM_STATE_FIELD_POSITION_X,
    SIM_STATE_FIELD_POSITION_Y,
    SIM_STATE_FIELD_VELOCITY_X,
    SIM_STATE_FIELD_VELOCITY_Y,
    SIM_STATE_FIELD_HEALTH,
    SIM_STATE_FIELD_COOLDOWN,
} SimStateField;

static const uint32_t SIM_STATE_DIFF_NO_UNIT = UINT32_MAX;

typedef struct SimStateDiff {
    SimStateField field;
    uint32_t unit_index;
    uint64_t expected_value;
    uint64_t actual_value;
} SimStateDiff;

// FNV-1a/64 over explicit little-endian fields in canonical array-major order:
// tick, count, RNG state/inc, then each live SoA array. Returns 0 for null or an
// invalid live count. It never hashes padding, pointers, or unused capacity.
uint64_t sim_hash_state(const SimWorld* world);

// Returns true and fills out_diff with the first canonical difference. Identical
// states return false with field NONE. Invalid inputs report field INVALID.
bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff);
const char* sim_state_field_name(SimStateField field);
