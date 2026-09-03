#pragma once

#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"
#include "math/rng.h"
#include "sim/components.h"
#include "sim/entity.h"
#include "sim/events.h"
#include "sim/hero.h"
#include "sim/map.h"
#include "sim/sim_config.h"

// SIM_MAX_UNITS is the replay command seam: commands address units by slot, and the
// replay codec records them per tick (G23). 64 is a v1 cap sized for the placeholder
// world; widening it is a replay-format + logic-hash break, triggered by a Phase 5
// scale test showing >64 commanded units per tick are needed.
static const uint32_t SIM_MAX_UNITS = 64;
static const uint32_t SIM_MAX_PLAYERS = 10;
static const uint32_t SIM_MAX_COMMANDS_PER_TICK = 256;
static const uint32_t SIM_DEFAULT_MAX_ENTITIES = 16384;
static const uint32_t SIM_DEFAULT_DAMAGE_EVENT_CAPACITY = SIM_MAX_COMMANDS_PER_TICK;

typedef enum SimCommandKind : uint8_t {
    SIM_COMMAND_SET_VELOCITY = 1,
    SIM_COMMAND_DAMAGE = 2,
    // M5.2. Both fit the frozen 16-byte record, so the replay v1 codec and the
    // placeholder generator are untouched. ATTACK: unit_index is the acting hero's
    // slot, value_x is fix_from_int(target slot), value_y and amount are zero.
    // CAST: unit_index is the acting hero's slot, amount is the action slot, and
    // value_x/value_y are read according to the action's target_mode, which comes
    // from the def table and never from the wire.
    SIM_COMMAND_ATTACK = 3,
    SIM_COMMAND_CAST = 4,
} SimCommandKind;

// Canonical placeholder command record. Unused fields must be zero: SET_VELOCITY
// uses value_x/value_y; DAMAGE uses amount. Replay encoding writes each field
// explicitly and never serializes this struct's memory image.
typedef struct SimCommand {
    uint8_t kind;
    uint8_t player_id;
    uint16_t unit_index;
    mm::fix value_x;
    mm::fix value_y;
    int32_t amount;
} SimCommand;

static_assert(sizeof(SimCommand) == 16, "the in-memory placeholder command should stay compact");

typedef struct SimCommandBuffer {
    const SimCommand* commands;
    uint32_t count;
} SimCommandBuffer;

// map: M5.0 grid capacity. All-zero is the empty map (the placeholder world has
// none); gameplay slices size it from the authored .mapdesc.
// hero_*/projectile_*/status_*/sim_event_capacity: M5.2 hero-combat capacity. Each
// is ZERO in sim_world_config_default(), and a zero capacity means the feature is
// absent entirely - no arena bytes (add_required rejects a zero-size request, so a
// zero-capacity pool is skipped the way an empty map is), no pool, every query
// reports absent. Gameplay slices size them from the authored hero content.
typedef struct SimWorldConfig {
    uint32_t max_entities;
    uint32_t initial_unit_count;
    uint32_t damage_event_capacity;
    MapConfig map;
    uint16_t hero_def_capacity;
    uint16_t hero_capacity;
    uint16_t projectile_capacity;
    uint16_t status_capacity;
    uint32_t sim_event_capacity;
} SimWorldConfig;

typedef struct SimWorld {
    uint64_t tick;
    SimWorldConfig config;
    mm::pcg32 rng;
    EntityManager entities;
    TransformPool transforms;
    VelocityPool velocities;
    HealthPool health;
    EntityId unit_entities[SIM_MAX_UNITS];
    DamageEventQueue damage_events;
    EntityId* pending_destroy;
    uint32_t pending_destroy_count;
    MapGrid map;
    SimHeroDefTable hero_defs;
    HeroPool heroes;
    ProjectilePool projectiles;
    StatusPool statuses;
    SimEventQueue sim_events;
} SimWorld;

SimWorldConfig sim_world_config_default(void);
size_t sim_world_memory_required(SimWorldConfig config);

// Initialization is transactional. Invalid configuration or insufficient arena
// budget returns false without changing the world or the arena offset.
bool sim_init(SimWorld* world, Arena* arena, uint64_t seed, SimWorldConfig config);
bool sim_destroy_deferred(SimWorld* world, EntityId entity);

// Installs one validated hero def before the first tick. The table is immutable
// while ticking: a mid-match def change is a schema break, not a hash bump.
// Rejection leaves the table and the world unchanged.
bool sim_install_hero_def(SimWorld* world, const SimHeroDef* def, uint16_t* out_index);
bool sim_command_is_canonical(const SimCommand* command, uint32_t player_count, uint32_t unit_count);
bool sim_validate_commands(const SimWorld* world, const SimCommandBuffer* commands);
bool sim_tick(SimWorld* world, const SimCommandBuffer* commands);
