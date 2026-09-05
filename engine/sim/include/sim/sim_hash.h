#pragma once

#include <stdint.h>

#include "sim/sim.h"

typedef enum SimStateField : uint8_t {
    SIM_STATE_FIELD_NONE = 0,
    SIM_STATE_FIELD_INVALID,
    SIM_STATE_FIELD_TICK,
    SIM_STATE_FIELD_CONFIG_MAX_ENTITIES,
    SIM_STATE_FIELD_CONFIG_INITIAL_UNIT_COUNT,
    SIM_STATE_FIELD_CONFIG_DAMAGE_EVENT_CAPACITY,
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
    SIM_STATE_FIELD_DAMAGE_EVENT_READ_COUNT,
    SIM_STATE_FIELD_DAMAGE_EVENT_READ_SOURCE,
    SIM_STATE_FIELD_DAMAGE_EVENT_READ_TARGET,
    SIM_STATE_FIELD_DAMAGE_EVENT_READ_AMOUNT,
    SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_COUNT,
    SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_SOURCE,
    SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_TARGET,
    SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_AMOUNT,
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
    // M5.0 map block (appended after Health so earlier first-divergence reports keep
    // their meaning): scalars, then cells in ascending index, then lanes.
    SIM_STATE_FIELD_MAP_ID,
    SIM_STATE_FIELD_MAP_WIDTH,
    SIM_STATE_FIELD_MAP_HEIGHT,
    SIM_STATE_FIELD_MAP_CELL_SIZE,
    SIM_STATE_FIELD_MAP_CELL_FLAGS,
    SIM_STATE_FIELD_MAP_CELL_COST,
    SIM_STATE_FIELD_MAP_CELL_HEIGHT,
    SIM_STATE_FIELD_MAP_LANE_COUNT,
    SIM_STATE_FIELD_MAP_LANE_ID,
    SIM_STATE_FIELD_MAP_LANE_WAYPOINT_COUNT,
    SIM_STATE_FIELD_MAP_LANE_WAVE_INTERVAL,
    SIM_STATE_FIELD_MAP_LANE_FIRST_WAVE,
    SIM_STATE_FIELD_MAP_LANE_ARCHETYPE,
    SIM_STATE_FIELD_MAP_LANE_WAYPOINT,
    // M5.2 hero-combat block, appended after the map block for the same reason the
    // map block was appended after Health: every earlier first-divergence report
    // keeps its meaning. The def table is installed whole, validated whole, and is
    // immutable while ticking, so a divergence there is reported per record.
    SIM_STATE_FIELD_HERO_DEF_COUNT,
    SIM_STATE_FIELD_HERO_DEF_RECORD,
    SIM_STATE_FIELD_HERO_COUNT,
    SIM_STATE_FIELD_HERO_ENTITY,
    SIM_STATE_FIELD_HERO_DEF_INDEX,
    SIM_STATE_FIELD_HERO_RESOURCE,
    SIM_STATE_FIELD_HERO_BASIC_ATTACK_COOLDOWN,
    SIM_STATE_FIELD_HERO_ACTION_COOLDOWN,
    SIM_STATE_FIELD_HERO_PENDING_KIND,
    SIM_STATE_FIELD_HERO_PENDING_SLOT,
    SIM_STATE_FIELD_HERO_PENDING_TARGET,
    SIM_STATE_FIELD_HERO_PENDING_POINT_X,
    SIM_STATE_FIELD_HERO_PENDING_POINT_Y,
    SIM_STATE_FIELD_PROJECTILE_COUNT,
    SIM_STATE_FIELD_PROJECTILE_ENTITY,
    SIM_STATE_FIELD_PROJECTILE_SOURCE,
    SIM_STATE_FIELD_PROJECTILE_TARGET,
    SIM_STATE_FIELD_PROJECTILE_DEF_INDEX,
    SIM_STATE_FIELD_PROJECTILE_ACTION_SLOT,
    SIM_STATE_FIELD_PROJECTILE_EFFECT_INDEX,
    SIM_STATE_FIELD_PROJECTILE_POSITION_X,
    SIM_STATE_FIELD_PROJECTILE_POSITION_Y,
    SIM_STATE_FIELD_PROJECTILE_SPEED,
    SIM_STATE_FIELD_PROJECTILE_REMAINING_TICKS,
    SIM_STATE_FIELD_STATUS_COUNT,
    SIM_STATE_FIELD_STATUS_ENTITY,
    SIM_STATE_FIELD_STATUS_EFFECT_TYPE,
    SIM_STATE_FIELD_STATUS_STACK_COUNT,
    SIM_STATE_FIELD_STATUS_REMAINING_TICKS,
    SIM_STATE_FIELD_STATUS_MAGNITUDE,
    SIM_STATE_FIELD_STATUS_SCALAR,
    SIM_STATE_FIELD_SIM_EVENT_READ_COUNT,
    SIM_STATE_FIELD_SIM_EVENT_READ_TICK,
    SIM_STATE_FIELD_SIM_EVENT_READ_KIND,
    SIM_STATE_FIELD_SIM_EVENT_READ_PAYLOAD,
    SIM_STATE_FIELD_SIM_EVENT_WRITE_COUNT,
    SIM_STATE_FIELD_SIM_EVENT_WRITE_TICK,
    SIM_STATE_FIELD_SIM_EVENT_WRITE_KIND,
    SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD,
    // M5.3 lane-objective block, appended after the SimEvent block for the same
    // reason every earlier block was appended: every existing first-divergence
    // report keeps its meaning. SIM_STATE_FIELD_HEALTH_LAST_DAMAGE_SOURCE is the one
    // exception to "enum order is hash order" - it is hashed INSIDE the Health block
    // because it is a Health field, but its enum position is appended here. Enum
    // position is an ABI-ish detail; hash position is the contract.
    SIM_STATE_FIELD_HEALTH_LAST_DAMAGE_SOURCE,
    SIM_STATE_FIELD_MATCH_TEAM_COUNT,
    SIM_STATE_FIELD_MATCH_OBJECTIVE_COUNT,
    SIM_STATE_FIELD_MATCH_ECONOMY_COUNT,
    SIM_STATE_FIELD_MATCH_MINIONS_PER_WAVE,
    SIM_STATE_FIELD_MATCH_CREEP_RECORD,
    SIM_STATE_FIELD_MATCH_TEAM_RECORD,
    SIM_STATE_FIELD_MATCH_OBJECTIVE_RECORD,
    SIM_STATE_FIELD_MATCH_ECONOMY_RECORD,
    SIM_STATE_FIELD_TEAM_COUNT,
    SIM_STATE_FIELD_TEAM_ENTITY,
    SIM_STATE_FIELD_TEAM_SIDE,
    SIM_STATE_FIELD_MINION_COUNT,
    SIM_STATE_FIELD_MINION_ENTITY,
    SIM_STATE_FIELD_MINION_LANE,
    SIM_STATE_FIELD_MINION_WAYPOINT_INDEX,
    SIM_STATE_FIELD_MINION_STATE,
    SIM_STATE_FIELD_MINION_TARGET,
    SIM_STATE_FIELD_MINION_ATTACK_COOLDOWN,
    SIM_STATE_FIELD_OBJECTIVE_COUNT,
    SIM_STATE_FIELD_OBJECTIVE_ENTITY,
    SIM_STATE_FIELD_OBJECTIVE_DEF_INDEX,
    SIM_STATE_FIELD_OBJECTIVE_OWNER_TEAM,
    SIM_STATE_FIELD_OBJECTIVE_KIND,
    SIM_STATE_FIELD_OBJECTIVE_ATTACK_COOLDOWN,
    SIM_STATE_FIELD_OBJECTIVE_STATE,
    SIM_STATE_FIELD_LEDGER_GOLD,
    SIM_STATE_FIELD_LEDGER_XP,
    SIM_STATE_FIELD_MATCH_STATE_OVER,
    SIM_STATE_FIELD_MATCH_STATE_WINNER,
    SIM_STATE_FIELD_MATCH_STATE_END_TICK,
} SimStateField;

static const uint32_t SIM_STATE_DIFF_NO_INDEX = UINT32_MAX;

typedef struct SimStateDiff {
    SimStateField field;
    uint32_t index;
    uint64_t expected_value;
    uint64_t actual_value;
} SimStateDiff;

// FNV-1a/64 over explicit little-endian fields:
//   tick; config max/initial/event capacity; RNG state/inc; entity capacity/live/next/free;
//   generations then liveness through next_fresh; free-stack order; all 64 unit
//   mappings; pending-destroy count/order; logical damage read/write phases;
//   then Transform, Velocity, and Health;
//   then the M5.0 map: map_id, width, height, cell_size, every cell's flags/cost/
//   height in ascending index, lane_count, and each lane's scalars + waypoints;
//   then the M5.2 hero block: the def table (count, then each def's scalars and each
//   action's and effect's scalars), HeroPool, ProjectilePool, StatusPool, and the
//   SimEvent read then write phase (count, then per event tick, kind, payload_size,
//   append_ordinal, and exactly payload_size payload bytes - never the tail padding);
//   then the M5.3 lane-objective block: the SimMatchDef's explicit fields (never the
//   struct image and never a trailing unused slot), TeamPool, MinionPool,
//   ObjectivePool, the SimLedger, and the SimMatchState. HealthPool's
//   last_damage_source is hashed INSIDE the Health block, appended after
//   damage_cooldown per row, because it is a Health field.
// Each component pool encodes count, then a membership byte per allocated entity
// index and, when present, exact EntityId plus its SoA fields. This makes entity
// index the semantic order and excludes pointers, padding, unused capacity,
// allocator state, sparse/dense storage order, ordered-query caches, physical
// event-buffer indices, and unused event capacity. Invalid worlds hash to zero.
uint64_t sim_hash_state(const SimWorld* world);

// Walks the exact canonical order above. index is an entity index for generation,
// liveness, and component fields; a free-stack/queue ordinal for ordered entries;
// a unit slot for mappings; and UINT32_MAX for scalars.
bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff);
const char* sim_state_field_name(SimStateField field);
