#pragma once

#include <stdint.h>

#include "sim/sim.h"

typedef enum SimStateField : uint8_t {
    SIM_STATE_FIELD_NONE = 0,
    SIM_STATE_FIELD_INVALID,
    SIM_STATE_FIELD_TICK,
    SIM_STATE_FIELD_CONFIG_MAX_ENTITIES,
    SIM_STATE_FIELD_CONFIG_INITIAL_UNIT_COUNT,
    SIM_STATE_FIELD_RNG_STATE,
    SIM_STATE_FIELD_RNG_INC,
    SIM_STATE_FIELD_ENTITY_CAPACITY,
    SIM_STATE_FIELD_ENTITY_LIVE_COUNT,
    SIM_STATE_FIELD_ENTITY_NEXT_FRESH,
    SIM_STATE_FIELD_ENTITY_FREE_COUNT,
    SIM_STATE_FIELD_ENTITY_GENERATION,
    SIM_STATE_FIELD_ENTITY_LIVENESS,
    SIM_STATE_FIELD_ENTITY_FREE_STACK,
    SIM_STATE_FIELD_UNIT_ENTITY,
    SIM_STATE_FIELD_PENDING_DESTROY_COUNT,
    SIM_STATE_FIELD_PENDING_DESTROY_ENTITY,
    SIM_STATE_FIELD_TRANSFORM_COUNT,
    SIM_STATE_FIELD_TRANSFORM_ENTITY,
    SIM_STATE_FIELD_POSITION_X,
    SIM_STATE_FIELD_POSITION_Y,
    SIM_STATE_FIELD_FACING,
    SIM_STATE_FIELD_VELOCITY_COUNT,
    SIM_STATE_FIELD_VELOCITY_ENTITY,
    SIM_STATE_FIELD_VELOCITY_X,
    SIM_STATE_FIELD_VELOCITY_Y,
    SIM_STATE_FIELD_HEALTH_COUNT,
    SIM_STATE_FIELD_HEALTH_ENTITY,
    SIM_STATE_FIELD_HEALTH_CURRENT,
    SIM_STATE_FIELD_HEALTH_MAXIMUM,
    SIM_STATE_FIELD_DAMAGE_COOLDOWN,
} SimStateField;

static const uint32_t SIM_STATE_DIFF_NO_INDEX = UINT32_MAX;

typedef struct SimStateDiff {
    SimStateField field;
    uint32_t index;
    uint64_t expected_value;
    uint64_t actual_value;
} SimStateDiff;

// FNV-1a/64 over explicit little-endian fields:
//   tick; config max/initial; RNG state/inc; entity capacity/live/next/free;
//   generations then liveness through next_fresh; free-stack order; all 64 unit
//   mappings; pending count/order; then Transform, Velocity, and Health.
// Each component pool encodes count, then a membership byte per allocated entity
// index and, when present, exact EntityId plus its SoA fields. This makes entity
// index the semantic order and excludes pointers, padding, unused capacity,
// allocator state, and sparse/dense storage order. Invalid worlds hash to zero.
uint64_t sim_hash_state(const SimWorld* world);

// Walks the exact canonical order above. index is an entity index for generation,
// liveness, and component fields; a free-stack/queue ordinal for ordered entries;
// a unit slot for mappings; and UINT32_MAX for scalars.
bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff);
const char* sim_state_field_name(SimStateField field);
