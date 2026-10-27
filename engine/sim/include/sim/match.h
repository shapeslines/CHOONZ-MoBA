#pragma once

#include <stddef.h>
#include <stdint.h>

#include "math/fix.h"
#include "sim/map.h"
#include "sim/team.h"

// M5.3 match data (m5-lane-objectives). eng_sim may include only engine/sim, core,
// math, and serialize, so it cannot read the asset layer's MatchDef / ObjectiveDef /
// EconomyRule. These structs are the sim-owned POD mirror of
// docs/slate-moba-proto-design.md sections 3.2 and 3.3, field for field, in the same
// order - exactly the arrangement hero.h uses for HeroDef. A later game-layer slice
// (eng_game links both) is the only place allowed to translate authored bytes into
// these records.
//
// Everything is fixed extent, integer only, and POD. Q16.16 scalars follow ADR-0002;
// nothing here reads platform, renderer, wall clock, or RNG state. Every reserved and
// pad field is explicit so the record carries no implicit tail padding: the canonical
// hash must never read a byte the compiler chose.

static const uint16_t SIM_MAX_OBJECTIVES = 8;
static const uint16_t SIM_MAX_ECONOMY_RULES = 8;
static const uint16_t SIM_MAX_MINIONS_PER_WAVE = 16;

// v1 tower offensive tuning. Section 3.3's ObjectiveDef carries no offensive fields
// and widening the mirror would break the field-for-field contract, so the sim pins
// range, magnitude, and cadence here, exactly as hero.h pins the basic attack.
static const int32_t SIM_TOWER_ATTACK_MAGNITUDE = 6;
static const uint32_t SIM_TOWER_ATTACK_COOLDOWN_TICKS = 12;
static const mm::fix SIM_TOWER_ATTACK_RANGE = 4 * FIX_ONE;

typedef enum SimObjectiveKind : uint8_t {
    SIM_OBJECTIVE_NONE = 0,
    SIM_OBJECTIVE_TOWER = 1,
    SIM_OBJECTIVE_CORE = 2,
} SimObjectiveKind;

typedef enum SimTargetPolicy : uint8_t {
    SIM_TARGET_POLICY_NONE = 0,
    SIM_TARGET_POLICY_TOWER = 1,     // hero-attacker > nearest minion > nearest hero
} SimTargetPolicy;

typedef enum SimAwardKind : uint8_t {
    SIM_AWARD_NONE = 0,
    SIM_AWARD_GOLD = 1,
    SIM_AWARD_XP = 2,
} SimAwardKind;

typedef enum SimAwardSource : uint8_t {
    SIM_AWARD_SOURCE_NONE = 0,
    SIM_AWARD_SOURCE_HERO_KILL = 1,
    SIM_AWARD_SOURCE_MINION_KILL = 2,
    SIM_AWARD_SOURCE_OBJECTIVE_DAMAGE = 3,       // accepted and hashed; awards nothing in v1
    SIM_AWARD_SOURCE_OBJECTIVE_DESTROYED = 4,
} SimAwardSource;

typedef struct SimObjectiveDef {     // mirrors ObjectiveDef, field for field
    uint64_t objective_id;           // AssetId
    uint8_t owner_team;              // 0 or 1
    uint8_t kind;                    // SimObjectiveKind, TOWER or CORE
    uint16_t reserved;               // must be 0
    int32_t max_health;              // > 0
    int32_t armor;                   // >= 0; mitigation is a constant 0 in v1
    uint8_t target_policy;           // SimTargetPolicy
    uint8_t pad[3];                  // must be 0
} SimObjectiveDef;

typedef struct SimEconomyRule {      // mirrors EconomyRule, field for field
    uint8_t award_kind;              // SimAwardKind, non-NONE
    uint8_t source_kind;             // SimAwardSource, non-NONE
    uint16_t reserved;               // must be 0
    uint32_t award_amount;           // may be 0 (a rule that awards nothing is legal)
} SimEconomyRule;

typedef struct SimMinionDef {        // the creep archetype; no section 3.2/3.3 counterpart
    uint64_t archetype_id;           // must equal MapLane.creep_archetype_id of its lane
    int32_t max_health;              // > 0
    mm::fix move_speed;              // > 0, Q16.16 world units per tick
    mm::fix attack_range;            // > 0
    int32_t attack_magnitude;        // > 0
    uint16_t attack_cooldown_ticks;  // > 0
    uint16_t reserved;               // must be 0
    uint32_t reserved2;              // must be 0; keeps the record 32 bytes with no
} SimMinionDef;                      // implicit tail padding

typedef struct SimTeamDef {          // the section 3.2 TeamDef reduced to sim indices
    uint8_t team_id;                 // must equal its array index
    uint8_t lane_id;                 // must name a lane present in world->map
    uint16_t spawn_ordinal;          // argument to map_find_spawn
    uint16_t tower_objective_index;  // < objective_count
    uint16_t core_objective_index;   // < objective_count
    uint32_t tower_cell;             // in bounds, MAP_CELL_OBJECTIVE flagged
    uint32_t core_cell;              // in bounds, MAP_CELL_OBJECTIVE flagged
    uint8_t waypoint_reverse;        // 0 or 1: walk the lane's waypoints descending
    uint8_t pad[3];                  // must be 0
} SimTeamDef;

typedef struct SimMatchDef {
    uint16_t team_count;             // 0 (absent) or exactly 2
    uint16_t objective_count;        // <= SIM_MAX_OBJECTIVES
    uint16_t economy_count;          // <= SIM_MAX_ECONOMY_RULES
    uint16_t minions_per_wave;       // <= SIM_MAX_MINIONS_PER_WAVE
    SimMinionDef creep;
    SimTeamDef teams[SIM_MAX_TEAMS];
    SimObjectiveDef objectives[SIM_MAX_OBJECTIVES];
    SimEconomyRule economy[SIM_MAX_ECONOMY_RULES];
} SimMatchDef;

static_assert(sizeof(SimObjectiveDef) == 24u, "SimObjectiveDef mirrors ObjectiveDef as a 24-byte POD");
static_assert(sizeof(SimEconomyRule) == 8u, "SimEconomyRule mirrors EconomyRule as an 8-byte POD");
static_assert(sizeof(SimMinionDef) == 32u, "SimMinionDef stays a fixed-extent 32-byte POD");
static_assert(sizeof(SimTeamDef) == 20u, "SimTeamDef stays a fixed-extent 20-byte POD");
static_assert(sizeof(SimMatchDef) == 336u, "SimMatchDef stays a fixed-extent 336-byte POD");

// ------------------------------------------------------------------ ledgers

typedef struct SimLedger {
    int32_t gold[SIM_MAX_TEAMS];
    int32_t xp[SIM_MAX_TEAMS];
} SimLedger;

typedef struct SimMatchState {
    uint8_t over;                    // 0 or 1
    uint8_t winner;                  // team id when over, SIM_TEAM_NONE otherwise
    uint16_t reserved;               // must be 0
    uint32_t end_tick;               // the tick `over` was set; 0 while not over
} SimMatchState;

static_assert(sizeof(SimLedger) == 16u, "SimLedger stays a 16-byte POD");
static_assert(sizeof(SimMatchState) == 8u, "SimMatchState stays an 8-byte POD");

// Ledger accumulation saturates: an award clamps at INT32_MAX and never wraps.
// Signed overflow is UB, would trip the UBSan gate, and a wrapped ledger is not a
// determinism-safe state.
int32_t sim_ledger_add_saturating(int32_t total, uint32_t amount);

// ------------------------------------------------------------------ validity
// Section 5 invalid-schema: a def is accepted whole or rejected whole. Unused
// trailing slots must be all zero bytes so one intent has exactly one byte pattern.

bool sim_objective_def_valid(const SimObjectiveDef* def, uint16_t team_count);
bool sim_economy_rule_valid(const SimEconomyRule* rule);
bool sim_minion_def_valid(const SimMinionDef* def);

// True when the whole record is zero bytes: the one byte pattern of "no match".
bool sim_match_def_is_absent(const SimMatchDef* def);
bool sim_match_def_valid(const SimMatchDef* def);
bool sim_match_def_valid_against_map(const SimMatchDef* def, const MapGrid* map);

// Resolves the lane index (not lane_id) a team walks, or reports absent.
bool sim_match_lane_index(const SimMatchDef* def, const MapGrid* map, uint8_t team_id,
                          uint16_t* out_lane_index);

struct SimWorld;

// Installs one validated match def before the first tick. The def is immutable while
// ticking: a mid-match change is a schema break, not a hash bump. Rejection leaves
// the world untouched, and install creates nothing - spawning the objective entities
// is sim_spawn_objective's job (sim/objectives.h).
bool sim_install_match_def(SimWorld* world, const SimMatchDef* def);
