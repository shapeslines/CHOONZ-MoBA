#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"

// M5.2 hero data (m5-hero-combat). eng_sim may include only engine/sim, core, math,
// and serialize, so it cannot read the asset layer's HeroDef. These structs are the
// sim-owned POD mirror of docs/slate-moba-proto-design.md section 3.3, field for
// field, in the same order. A later game-layer slice (eng_game links both) is the
// only place allowed to translate authored HeroDef bytes into these records.
//
// Everything is fixed extent, integer only, and arena backed. Q16.16 scalars follow
// ADR-0002; nothing here reads platform, renderer, wall clock, or RNG state.

static const uint16_t SIM_MAX_ACTION_SLOTS = 8;          // section 3.3 fixture uses 3
static const uint16_t SIM_MAX_EFFECTS_PER_ACTION = 4;
static const uint8_t SIM_MAX_DAMAGE_TYPE = 2;            // physical, magic, pure

// v1 placeholder basic-attack tuning. Section 3.3's HeroDef carries no basic-attack
// magnitude or cadence, and widening the mirror would break the field-for-field
// contract, so the sim pins them here until the authored schema grows the fields.
// The magnitude matches the M5.1 intake placeholder so the two seams agree.
static const int32_t SIM_BASIC_ATTACK_MAGNITUDE = 1;
static const uint32_t SIM_BASIC_ATTACK_COOLDOWN_TICKS = 10;
// A projectile that never reaches its target must still be bounded state.
static const uint16_t SIM_PROJECTILE_MAX_LIFETIME_TICKS = 300;

typedef enum SimEffectType : uint8_t {
    SIM_EFFECT_NONE = 0,
    SIM_EFFECT_PROJECTILE_DAMAGE = 1,
    SIM_EFFECT_SELF_HEAL = 2,
    SIM_EFFECT_AREA_SLOW = 3,
} SimEffectType;

typedef enum SimTargetMode : uint8_t {
    SIM_TARGET_POINT = 0,
    SIM_TARGET_ENTITY = 1,
    SIM_TARGET_SELF = 2,
    SIM_TARGET_AREA = 3,
} SimTargetMode;

typedef struct SimEffectDef {          // mirrors EffectDef
    uint8_t effect_type;
    uint8_t damage_type;
    uint16_t duration_ticks;
    int32_t magnitude;
    mm::fix radius;                    // radius_q16
    mm::fix scalar;                    // scalar_q16
} SimEffectDef;

typedef struct SimActionDef {          // mirrors ActionDef
    uint64_t action_id;
    uint8_t slot;
    uint8_t target_mode;
    uint16_t effect_count;
    uint32_t cooldown_ticks;
    uint32_t cast_time_ticks;          // stored and validated; not simulated in v1
    uint32_t resource_cost;
    mm::fix range;                     // range_q16
    mm::fix projectile_speed;          // projectile_speed_q16
    SimEffectDef effects[SIM_MAX_EFFECTS_PER_ACTION];
} SimActionDef;

typedef struct SimHeroDef {            // mirrors HeroDef; schema_version stays in the
    uint64_t hero_def_id;              // asset layer and never enters the simulation
    int32_t max_health;
    mm::fix move_speed;                // move_speed_q16
    mm::fix attack_range;              // attack_range_q16
    uint16_t action_count;
    SimActionDef actions[SIM_MAX_ACTION_SLOTS];
} SimHeroDef;

static_assert(sizeof(SimEffectDef) == 16u, "SimEffectDef mirrors EffectDef as a 16-byte POD");
static_assert(sizeof(SimActionDef) == 96u, "SimActionDef stays a fixed-extent POD");
static_assert(sizeof(SimHeroDef) == 792u, "SimHeroDef stays a fixed-extent POD");

// Section 5 invalid-schema: a def is accepted whole or rejected whole. Unused
// trailing actions and effects must be zero so one intent has one byte pattern.
bool sim_effect_def_valid(const SimEffectDef* effect);
bool sim_action_def_valid(const SimActionDef* action);
bool sim_hero_def_valid(const SimHeroDef* def);

// Bounded def table owned by SimWorld. Capacity zero means no table at all: no
// arena bytes, and every lookup reports absent.
typedef struct SimHeroDefTable {
    SimHeroDef* defs;
    uint16_t capacity;
    uint16_t count;
} SimHeroDefTable;

size_t sim_hero_def_table_memory_required(uint16_t capacity);

// Failure is atomic: table contents and arena offset are unchanged.
bool sim_hero_def_table_init(SimHeroDefTable* table, Arena* arena, uint16_t capacity);
bool sim_hero_def_table_valid(const SimHeroDefTable* table);
const SimHeroDef* sim_hero_def_table_get(const SimHeroDefTable* table, uint16_t index);

// Appends one validated def and reports its index. Rejection mutates nothing.
bool sim_hero_def_table_install(SimHeroDefTable* table, const SimHeroDef* def,
                                uint16_t* out_index);
