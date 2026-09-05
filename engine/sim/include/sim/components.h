#pragma once

#include <stddef.h>
#include <stdint.h>

#include "math/fix.h"
#include "sim/component_pool.h"
#include "sim/hero.h"

typedef struct TransformView {
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* facing;
} TransformView;

typedef struct ConstTransformView {
    const mm::fix* position_x;
    const mm::fix* position_y;
    const mm::fix* facing;
} ConstTransformView;

typedef struct TransformPool {
    ComponentPool membership;
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* facing;
} TransformPool;

size_t transform_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool transform_pool_init(TransformPool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity);
bool transform_pool_add(TransformPool* pool, EntityId entity,
                        mm::fix position_x, mm::fix position_y, mm::fix facing);
bool transform_pool_remove(TransformPool* pool, EntityId entity);
bool transform_pool_has(const TransformPool* pool, EntityId entity);
bool transform_pool_get(TransformPool* pool, EntityId entity, TransformView* view);
bool transform_pool_get_const(const TransformPool* pool, EntityId entity,
                              ConstTransformView* view);

typedef struct VelocityView {
    mm::fix* velocity_x;
    mm::fix* velocity_y;
} VelocityView;

typedef struct VelocityPool {
    ComponentPool membership;
    mm::fix* velocity_x;
    mm::fix* velocity_y;
} VelocityPool;

size_t velocity_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool velocity_pool_init(VelocityPool* pool, Arena* arena,
                        uint32_t entity_capacity, uint32_t capacity);
bool velocity_pool_add(VelocityPool* pool, EntityId entity,
                       mm::fix velocity_x, mm::fix velocity_y);
bool velocity_pool_remove(VelocityPool* pool, EntityId entity);
bool velocity_pool_has(const VelocityPool* pool, EntityId entity);
bool velocity_pool_get(VelocityPool* pool, EntityId entity, VelocityView* view);

typedef struct HealthView {
    int32_t* current;
    int32_t* maximum;
    uint32_t* damage_cooldown;
    EntityId* last_damage_source;
} HealthView;

// last_damage_source (M5.3) is the sole kill-credit input. It is written only in
// combat.cpp, on the damage commit inside sys_combat_resolve; health_pool_add starts
// it at HANDLE_NULL and health_pool_remove clears it, so a recycled entity index can
// never inherit a stale killer. It is authoritative between the commit and the death
// verdict, so it is hashed inside the Health block.
typedef struct HealthPool {
    ComponentPool membership;
    int32_t* current;
    int32_t* maximum;
    uint32_t* damage_cooldown;
    EntityId* last_damage_source;
} HealthPool;

size_t health_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool health_pool_init(HealthPool* pool, Arena* arena,
                      uint32_t entity_capacity, uint32_t capacity);
bool health_pool_add(HealthPool* pool, EntityId entity,
                     int32_t current, int32_t maximum, uint32_t damage_cooldown);
bool health_pool_remove(HealthPool* pool, EntityId entity);
bool health_pool_has(const HealthPool* pool, EntityId entity);
bool health_pool_get(HealthPool* pool, EntityId entity, HealthView* view);

// ------------------------------------------------------------------ M5.2 hero combat
// Three bounded SoA pools sharing the ComponentPool membership core, exactly like
// TransformPool. Every pool is optional: capacity zero leaves the whole struct
// zeroed, allocates no arena bytes, and reports every entity absent.

typedef enum SimHeroPending : uint8_t {
    SIM_HERO_PENDING_NONE = 0,
    SIM_HERO_PENDING_ATTACK = 1,
    SIM_HERO_PENDING_CAST = 2,
} SimHeroPending;

typedef struct HeroView {
    uint16_t* def_index;
    int32_t* resource;
    uint32_t* basic_attack_cooldown;
    uint32_t* action_cooldown;          // SIM_MAX_ACTION_SLOTS entries
    uint8_t* pending_kind;
    uint8_t* pending_slot;
    EntityId* pending_target;
    mm::fix* pending_point_x;
    mm::fix* pending_point_y;
} HeroView;

// pending_* is tick-local intent written by sys_apply_commands and consumed by
// sys_hero_actions. It is authoritative between those two steps, so it is hashed;
// sys_hero_actions clears it unconditionally before the step returns.
typedef struct HeroPool {
    ComponentPool membership;
    uint16_t* def_index;
    int32_t* resource;
    uint32_t* basic_attack_cooldown;
    uint32_t* action_cooldown;
    uint8_t* pending_kind;
    uint8_t* pending_slot;
    EntityId* pending_target;
    mm::fix* pending_point_x;
    mm::fix* pending_point_y;
} HeroPool;

size_t hero_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool hero_pool_init(HeroPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity);
bool hero_pool_add(HeroPool* pool, EntityId entity, uint16_t def_index, int32_t resource);
bool hero_pool_remove(HeroPool* pool, EntityId entity);
bool hero_pool_has(const HeroPool* pool, EntityId entity);
bool hero_pool_get(HeroPool* pool, EntityId entity, HeroView* view);
// Read-only def lookup for const seams such as sim_validate_commands.
bool hero_pool_def_index(const HeroPool* pool, EntityId entity, uint16_t* out_def_index);

typedef struct ProjectileView {
    EntityId* source;
    EntityId* target;
    uint16_t* def_index;
    uint8_t* action_slot;
    uint8_t* effect_index;
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* speed;
    uint16_t* remaining_ticks;
} ProjectileView;

typedef struct ProjectilePool {
    ComponentPool membership;
    EntityId* source;
    EntityId* target;
    uint16_t* def_index;
    uint8_t* action_slot;
    uint8_t* effect_index;
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* speed;
    uint16_t* remaining_ticks;
} ProjectilePool;

size_t projectile_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool projectile_pool_init(ProjectilePool* pool, Arena* arena,
                          uint32_t entity_capacity, uint32_t capacity);
bool projectile_pool_add(ProjectilePool* pool, EntityId entity, EntityId source,
                         EntityId target, uint16_t def_index, uint8_t action_slot,
                         uint8_t effect_index, mm::fix position_x, mm::fix position_y,
                         mm::fix speed, uint16_t remaining_ticks);
bool projectile_pool_remove(ProjectilePool* pool, EntityId entity);
bool projectile_pool_has(const ProjectilePool* pool, EntityId entity);
bool projectile_pool_get(ProjectilePool* pool, EntityId entity, ProjectileView* view);

typedef struct StatusView {
    uint8_t* effect_type;
    uint8_t* stack_count;
    uint16_t* remaining_ticks;
    int32_t* magnitude;
    mm::fix* scalar;
} StatusView;

// One row per target entity, tagged with the effect type it carries. Re-applying
// the same effect type refreshes the duration and keeps the larger magnitude;
// a different effect type replaces the row. v1 defines exactly one status effect,
// so the single-row rule is the whole (target, effect_type) rule.
typedef struct StatusPool {
    ComponentPool membership;
    uint8_t* effect_type;
    uint8_t* stack_count;
    uint16_t* remaining_ticks;
    int32_t* magnitude;
    mm::fix* scalar;
} StatusPool;

size_t status_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool status_pool_init(StatusPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity);
bool status_pool_add(StatusPool* pool, EntityId entity, uint8_t effect_type,
                     uint16_t remaining_ticks, int32_t magnitude, mm::fix scalar);
bool status_pool_remove(StatusPool* pool, EntityId entity);
bool status_pool_has(const StatusPool* pool, EntityId entity);
bool status_pool_get(StatusPool* pool, EntityId entity, StatusView* view);

// --------------------------------------------------------------- M5.3 lane objectives
// Two more bounded SoA pools on the same ComponentPool core. Both are optional:
// capacity zero leaves the whole struct zeroed, allocates no arena bytes, and reports
// every entity absent, so a world with no match ticks exactly as it did before M5.3.

typedef enum SimMinionState : uint8_t {
    SIM_MINION_PUSH = 0,
    SIM_MINION_ATTACK = 1,
    SIM_MINION_RETURN = 2,
} SimMinionState;

typedef struct MinionView {
    uint8_t* lane;
    uint8_t* waypoint_index;
    uint8_t* state;
    EntityId* target;
    uint32_t* attack_cooldown;
} MinionView;

typedef struct MinionPool {
    ComponentPool membership;
    uint8_t* lane;               // MapLane.lane_id this minion walks
    uint8_t* waypoint_index;     // < that lane's waypoint_count
    uint8_t* state;              // SimMinionState
    EntityId* target;            // HANDLE_NULL unless state == ATTACK
    uint32_t* attack_cooldown;   // ticks remaining; sys_cooldown_tick is the sole decrementer
} MinionPool;

size_t minion_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool minion_pool_init(MinionPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity);
bool minion_pool_add(MinionPool* pool, EntityId entity, uint8_t lane, uint8_t waypoint_index);
bool minion_pool_remove(MinionPool* pool, EntityId entity);
bool minion_pool_has(const MinionPool* pool, EntityId entity);
bool minion_pool_get(MinionPool* pool, EntityId entity, MinionView* view);

typedef enum SimObjectiveState : uint8_t {
    SIM_OBJECTIVE_ALIVE = 0,
    SIM_OBJECTIVE_DESTROYED = 1,
} SimObjectiveState;

typedef struct ObjectiveView {
    uint16_t* def_index;
    uint8_t* owner_team;
    uint8_t* kind;
    uint32_t* attack_cooldown;
    uint8_t* state;
} ObjectiveView;

// owner_team and kind are denormalized from the def so every AI walk is one array
// read rather than a def-table indirection; canonical_world_valid enforces the
// agreement, so the duplication can never drift.
typedef struct ObjectivePool {
    ComponentPool membership;
    uint16_t* def_index;         // < match.objective_count
    uint8_t* owner_team;
    uint8_t* kind;
    uint32_t* attack_cooldown;
    uint8_t* state;              // SimObjectiveState
} ObjectivePool;

size_t objective_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool objective_pool_init(ObjectivePool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity);
bool objective_pool_add(ObjectivePool* pool, EntityId entity, uint16_t def_index,
                        uint8_t owner_team, uint8_t kind);
bool objective_pool_remove(ObjectivePool* pool, EntityId entity);
bool objective_pool_has(const ObjectivePool* pool, EntityId entity);
bool objective_pool_get(ObjectivePool* pool, EntityId entity, ObjectiveView* view);
