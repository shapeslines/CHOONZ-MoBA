#pragma once

#include <stddef.h>
#include <stdint.h>

#include "math/fix.h"
#include "sim/entity.h"
#include "sim/sim.h"

// M5.1 command intake (m5-command-replay): the deterministic front door for player
// intent. Commands are validated, ordered by the canonical key, de-duplicated, and
// translated into the existing SimCommandBuffer that sim_tick consumes. The intake
// is a pure function of (world, config, input): it never mutates the world, never
// draws RNG, never allocates, and always produces the same accepted[] and rejects[]
// for the same input, so a server, a replay, and a test agree byte for byte.
//
// Shapes follow docs/slate-moba-proto-design.md section 3.4 in field order. The
// replay v1 record stays the legacy 16-byte SimCommand (ADR-0014, M6.0 unifies the
// wire codec on this shape); the intake sits in front of that seam, so the oracle
// and replay bytes are untouched by this slice.

typedef enum CommandKind : uint8_t {
    COMMAND_KIND_MOVE = 1,
    COMMAND_KIND_BASIC_ATTACK = 2,
    COMMAND_KIND_USE_ACTION = 3,
} CommandKind;

typedef struct Command {
    uint32_t tick;
    uint8_t player_id;
    uint8_t command_kind;
    uint16_t sequence;
    EntityId actor;
    uint64_t action_id;
    EntityId target;
    mm::fix point_x_q16;
    mm::fix point_y_q16;
} Command;

typedef enum CommandRejectReason : uint8_t {
    COMMAND_REJECT_NONE = 0,
    COMMAND_REJECT_MALFORMED,
    COMMAND_REJECT_WRONG_TICK,
    COMMAND_REJECT_STALE_ENTITY,
    COMMAND_REJECT_UNAUTHORIZED_ACTOR,
    COMMAND_REJECT_MATCH_OVER,
    COMMAND_REJECT_COOLDOWN,        // defined; produced by m5-hero-combat
    COMMAND_REJECT_RANGE,           // defined; produced by m5-hero-combat
    COMMAND_REJECT_CAPACITY,
    COMMAND_REJECT_DUPLICATE_SEQUENCE,
} CommandRejectReason;

typedef struct CommandReject {
    uint32_t tick;
    uint8_t player_id;
    uint8_t reason;
    uint16_t sequence;
    EntityId actor;
} CommandReject;

static_assert(sizeof(Command) == 40, "Command stays a compact POD record (36 bytes of fields, 8-aligned)");
static_assert(sizeof(CommandReject) == 12, "CommandReject stays a compact POD record");

// Per-tick intake parameters supplied by the authority (server or replay driver).
//   damage_capacity_remaining: free slots in the write phase of the damage event
//   queue; BASIC_ATTACK commands beyond it are rejected with CAPACITY at intake so
//   the accepted buffer always passes sim_validate_commands' preflight (ADR-0014:
//   reject at source, never drop).
typedef struct CommandIntakeConfig {
    uint32_t tick;
    uint8_t player_count;
    uint8_t match_over;
    uint32_t damage_capacity_remaining;
} CommandIntakeConfig;

// Canonical ordering key: (tick, player_id, sequence, actor.index, command_kind).
// Returns <0, 0, >0. Two commands with equal keys are duplicates by definition.
int command_key_compare(const Command* a, const Command* b);

// Placeholder ownership until TeamDef lands (proto-design section 3.2): player p
// owns unit slots [p * (SIM_MAX_UNITS / player_count), (p + 1) * ...).
bool command_player_owns_unit(uint32_t player_id, uint32_t player_count, uint32_t unit_index);

// Resolve a live actor handle to its unit slot in world->unit_entities.
bool command_actor_slot(const SimWorld* world, EntityId actor, uint32_t* out_slot);

// Validate, order, and de-duplicate one tick of commands.
//   accepted[]  receives at most `capacity` commands in canonical key order.
//   rejects[]   receives at most `capacity` rejects in input order.
// Returns false only for invalid arguments (null pointers, count > capacity,
// capacity > SIM_MAX_COMMANDS_PER_TICK); in that case nothing is written.
bool command_intake_run(const SimWorld* world, CommandIntakeConfig config,
                        const Command* input, uint32_t count, uint32_t capacity,
                        Command* accepted, uint32_t* out_accepted_count,
                        CommandReject* rejects, uint32_t* out_reject_count);

// Translate an accepted command into the legacy sim seam. MOVE -> SET_VELOCITY
// (point is the desired velocity until navigation lands), BASIC_ATTACK -> DAMAGE
// with a unit placeholder amount. USE_ACTION has no seam yet and returns false.
bool command_to_sim(const SimWorld* world, const Command* command, SimCommand* out);

// Translate a whole accepted batch; returns the number written (== count on success).
uint32_t command_batch_to_sim(const SimWorld* world, const Command* accepted, uint32_t count,
                              SimCommand* out);

const char* command_reject_reason_string(CommandRejectReason reason);
