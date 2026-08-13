#pragma once

#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"
#include "math/rng.h"
#include "sim/components.h"
#include "sim/entity.h"
#include "sim/events.h"
#include "sim/sim_config.h"

static const uint32_t SIM_MAX_UNITS = 64;
static const uint32_t SIM_MAX_PLAYERS = 10;
static const uint32_t SIM_MAX_COMMANDS_PER_TICK = 256;
static const uint32_t SIM_DEFAULT_MAX_ENTITIES = 16384;
static const uint32_t SIM_DEFAULT_DAMAGE_EVENT_CAPACITY = SIM_MAX_COMMANDS_PER_TICK;

typedef enum SimCommandKind : uint8_t {
    SIM_COMMAND_SET_VELOCITY = 1,
    SIM_COMMAND_DAMAGE = 2,
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

typedef struct SimWorldConfig {
    uint32_t max_entities;
    uint32_t initial_unit_count;
    uint32_t damage_event_capacity;
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
} SimWorld;

SimWorldConfig sim_world_config_default(void);
size_t sim_world_memory_required(SimWorldConfig config);

// Initialization is transactional. Invalid configuration or insufficient arena
// budget returns false without changing the world or the arena offset.
bool sim_init(SimWorld* world, Arena* arena, uint64_t seed, SimWorldConfig config);
bool sim_destroy_deferred(SimWorld* world, EntityId entity);
bool sim_command_is_canonical(const SimCommand* command, uint32_t player_count, uint32_t unit_count);
bool sim_validate_commands(const SimWorld* world, const SimCommandBuffer* commands);
bool sim_tick(SimWorld* world, const SimCommandBuffer* commands);
