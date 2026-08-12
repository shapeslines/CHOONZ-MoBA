#pragma once

#include <stdint.h>

#include "math/fix.h"
#include "math/rng.h"
#include "sim/sim_config.h"

static const uint32_t SIM_MAX_UNITS = 64;
static const uint32_t SIM_MAX_PLAYERS = 10;
static const uint32_t SIM_MAX_COMMANDS_PER_TICK = 256;

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

// M3.0 placeholder world: fixed-capacity SoA, deliberately not the M3.1 ECS.
// sim_init clears the complete object so equal runs are byte-identical, including
// padding and unused capacity; hashing will still encode only authoritative fields.
typedef struct SimWorld {
    uint64_t tick;
    uint32_t unit_count;
    mm::pcg32 rng;
    mm::fix position_x[SIM_MAX_UNITS];
    mm::fix position_y[SIM_MAX_UNITS];
    mm::fix velocity_x[SIM_MAX_UNITS];
    mm::fix velocity_y[SIM_MAX_UNITS];
    int32_t health[SIM_MAX_UNITS];
    uint32_t cooldown[SIM_MAX_UNITS];
} SimWorld;

void sim_init(SimWorld* world, uint64_t seed);
bool sim_command_is_canonical(const SimCommand* command, uint32_t player_count, uint32_t unit_count);
bool sim_validate_commands(const SimWorld* world, const SimCommandBuffer* commands);
bool sim_tick(SimWorld* world, const SimCommandBuffer* commands);
