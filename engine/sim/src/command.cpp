#include "sim/command.h"

#include <cstring>

static const int32_t COMMAND_BASIC_ATTACK_PLACEHOLDER_AMOUNT = 1;

// ---------------------------------------------------------------- key / ownership

int command_key_compare(const Command* a, const Command* b) {
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    if (a->player_id != b->player_id) return a->player_id < b->player_id ? -1 : 1;
    if (a->sequence != b->sequence) return a->sequence < b->sequence ? -1 : 1;
    uint32_t ai = handle_index(a->actor.h);
    uint32_t bi = handle_index(b->actor.h);
    if (ai != bi) return ai < bi ? -1 : 1;
    if (a->command_kind != b->command_kind) return a->command_kind < b->command_kind ? -1 : 1;
    return 0;
}

bool command_player_owns_unit(uint32_t player_id, uint32_t player_count, uint32_t unit_index) {
    if (player_count == 0u || player_count > SIM_MAX_PLAYERS || player_id >= player_count ||
        unit_index >= SIM_MAX_UNITS) return false;
    uint32_t span = SIM_MAX_UNITS / player_count;
    if (span == 0u) return false;
    uint32_t first = player_id * span;
    uint32_t last = (player_id + 1u == player_count) ? SIM_MAX_UNITS : first + span;
    return unit_index >= first && unit_index < last;
}

bool command_actor_slot(const SimWorld* world, EntityId actor, uint32_t* out_slot) {
    if (!world || !out_slot || actor.h == HANDLE_NULL) return false;
    if (!entity_manager_is_alive(&world->entities, actor)) return false;
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        if (world->unit_entities[slot].h == actor.h) {
            *out_slot = slot;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- validation

static bool kind_known(uint8_t kind) {
    return kind == COMMAND_KIND_MOVE || kind == COMMAND_KIND_BASIC_ATTACK ||
           kind == COMMAND_KIND_USE_ACTION;
}

// Shape rules: unknown kind, player out of range, and unused fields that are not
// zero are MALFORMED. This is the canonical-form rule that keeps one intent one
// byte pattern, the same discipline sim_command_is_canonical applies downstream.
static bool command_shape_ok(const Command* c, uint32_t player_count) {
    if (!kind_known(c->command_kind) || c->player_id >= player_count) return false;
    switch (c->command_kind) {
        case COMMAND_KIND_MOVE:
            return c->action_id == 0u && c->target.h == HANDLE_NULL;
        case COMMAND_KIND_BASIC_ATTACK:
            return c->action_id == 0u && c->point_x_q16 == 0 && c->point_y_q16 == 0;
        case COMMAND_KIND_USE_ACTION:
            return c->action_id != 0u;
        default:
            return false;
    }
}

static CommandRejectReason validate_one(const SimWorld* world, CommandIntakeConfig config,
                                        const Command* c) {
    if (config.match_over) return COMMAND_REJECT_MATCH_OVER;
    if (!command_shape_ok(c, config.player_count)) return COMMAND_REJECT_MALFORMED;
    if (c->tick != config.tick) return COMMAND_REJECT_WRONG_TICK;
    uint32_t slot = 0u;
    if (!command_actor_slot(world, c->actor, &slot)) return COMMAND_REJECT_STALE_ENTITY;
    if (!command_player_owns_unit(c->player_id, config.player_count, slot))
        return COMMAND_REJECT_UNAUTHORIZED_ACTOR;
    if (c->command_kind == COMMAND_KIND_BASIC_ATTACK) {
        if (c->target.h == HANDLE_NULL || !entity_manager_is_alive(&world->entities, c->target))
            return COMMAND_REJECT_STALE_ENTITY;
    }
    if (c->command_kind == COMMAND_KIND_USE_ACTION) {
        // No action table exists yet; every action is unknown, which is a shape
        // failure rather than a cooldown/range verdict.
        return COMMAND_REJECT_MALFORMED;
    }
    return COMMAND_REJECT_NONE;
}

static void push_reject(CommandReject* rejects, uint32_t* count, const Command* c,
                        CommandRejectReason reason) {
    CommandReject r{};
    r.tick = c->tick;
    r.player_id = c->player_id;
    r.reason = static_cast<uint8_t>(reason);
    r.sequence = c->sequence;
    r.actor = c->actor;
    rejects[(*count)++] = r;
}

// Branch-stable insertion sort: at most SIM_MAX_COMMANDS_PER_TICK records, and the
// comparator is a strict total order, so the output is unique for a given multiset.
static void sort_by_key(Command* items, uint32_t count) {
    for (uint32_t i = 1u; i < count; ++i) {
        Command key = items[i];
        uint32_t j = i;
        while (j > 0u && command_key_compare(&items[j - 1u], &key) > 0) {
            items[j] = items[j - 1u];
            --j;
        }
        items[j] = key;
    }
}

bool command_intake_run(const SimWorld* world, CommandIntakeConfig config,
                        const Command* input, uint32_t count, uint32_t capacity,
                        Command* accepted, uint32_t* out_accepted_count,
                        CommandReject* rejects, uint32_t* out_reject_count) {
    if (!world || !accepted || !out_accepted_count || !rejects || !out_reject_count) return false;
    if (capacity == 0u || capacity > SIM_MAX_COMMANDS_PER_TICK || count > capacity) return false;
    if (count > 0u && !input) return false;
    if (config.player_count == 0u || config.player_count > SIM_MAX_PLAYERS) return false;

    uint32_t accepted_count = 0u;
    uint32_t reject_count = 0u;

    // Pass 1: per-command validation in input order; survivors are staged in
    // input order so rejects[] reports the caller's ordinal.
    for (uint32_t i = 0u; i < count; ++i) {
        CommandRejectReason reason = validate_one(world, config, &input[i]);
        if (reason != COMMAND_REJECT_NONE) push_reject(rejects, &reject_count, &input[i], reason);
        else accepted[accepted_count++] = input[i];
    }

    // Pass 2: canonical order, then duplicate (player_id, sequence) removal keeps
    // the first in key order and rejects every later one.
    sort_by_key(accepted, accepted_count);
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < accepted_count; ++i) {
        bool duplicate = kept > 0u && accepted[kept - 1u].player_id == accepted[i].player_id &&
                         accepted[kept - 1u].sequence == accepted[i].sequence;
        if (duplicate) push_reject(rejects, &reject_count, &accepted[i], COMMAND_REJECT_DUPLICATE_SEQUENCE);
        else accepted[kept++] = accepted[i];
    }
    accepted_count = kept;

    // Pass 3: capacity, in key order so the same stream always keeps the same head.
    uint32_t damage_budget = config.damage_capacity_remaining;
    kept = 0u;
    for (uint32_t i = 0u; i < accepted_count; ++i) {
        bool over = false;
        if (accepted[i].command_kind == COMMAND_KIND_BASIC_ATTACK) {
            if (damage_budget == 0u) over = true;
            else --damage_budget;
        }
        if (!over && kept >= SIM_MAX_COMMANDS_PER_TICK) over = true;
        if (over) push_reject(rejects, &reject_count, &accepted[i], COMMAND_REJECT_CAPACITY);
        else accepted[kept++] = accepted[i];
    }
    accepted_count = kept;

    *out_accepted_count = accepted_count;
    *out_reject_count = reject_count;
    return true;
}

// ---------------------------------------------------------------- translation

bool command_to_sim(const SimWorld* world, const Command* command, SimCommand* out) {
    uint32_t slot = 0u;
    if (!command || !out || !command_actor_slot(world, command->actor, &slot)) return false;
    SimCommand sim{};
    sim.player_id = command->player_id;
    sim.unit_index = static_cast<uint16_t>(slot);
    switch (command->command_kind) {
        case COMMAND_KIND_MOVE:
            sim.kind = SIM_COMMAND_SET_VELOCITY;
            sim.value_x = command->point_x_q16;
            sim.value_y = command->point_y_q16;
            break;
        case COMMAND_KIND_BASIC_ATTACK: {
            uint32_t target_slot = 0u;
            if (!command_actor_slot(world, command->target, &target_slot)) return false;
            // M5.2: a hero attacks through the unified pipeline (ATTACK carries the
            // actor and names the target in value_x). A non-hero unit keeps the M5.1
            // DAMAGE placeholder, so worlds without a hero pool are byte-identical.
            if (hero_pool_has(&world->heroes, command->actor)) {
                sim.kind = SIM_COMMAND_ATTACK;
                sim.value_x = mm::fix_from_int(static_cast<int32_t>(target_slot));
                break;
            }
            sim.kind = SIM_COMMAND_DAMAGE;
            sim.unit_index = static_cast<uint16_t>(target_slot);
            sim.amount = COMMAND_BASIC_ATTACK_PLACEHOLDER_AMOUNT;
            break;
        }
        default:
            return false;
    }
    if (!sim_command_is_canonical(&sim, SIM_MAX_PLAYERS, SIM_MAX_UNITS)) return false;
    *out = sim;
    return true;
}

uint32_t command_batch_to_sim(const SimWorld* world, const Command* accepted, uint32_t count,
                              SimCommand* out) {
    if (!out || (count > 0u && !accepted)) return 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        if (!command_to_sim(world, &accepted[i], &out[i])) return i;
    }
    return count;
}

const char* command_reject_reason_string(CommandRejectReason reason) {
    switch (reason) {
        case COMMAND_REJECT_NONE: return "none";
        case COMMAND_REJECT_MALFORMED: return "malformed";
        case COMMAND_REJECT_WRONG_TICK: return "wrong_tick";
        case COMMAND_REJECT_STALE_ENTITY: return "stale_entity";
        case COMMAND_REJECT_UNAUTHORIZED_ACTOR: return "unauthorized_actor";
        case COMMAND_REJECT_MATCH_OVER: return "match_over";
        case COMMAND_REJECT_COOLDOWN: return "cooldown";
        case COMMAND_REJECT_RANGE: return "range";
        case COMMAND_REJECT_CAPACITY: return "capacity";
        case COMMAND_REJECT_DUPLICATE_SEQUENCE: return "duplicate_sequence";
        default: return "unknown";
    }
}
