#include "sim/sim.h"

#include <cstring>

static mm::fix fix_add_wrap(mm::fix a, mm::fix b) {
    return static_cast<mm::fix>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

void sim_init(SimWorld* world, uint64_t seed) {
    if (!world) return;
    std::memset(world, 0, sizeof(*world));
    world->unit_count = SIM_MAX_UNITS;
    mm::pcg32_seed(&world->rng, seed, 1u);
    for (uint32_t i = 0; i < world->unit_count; ++i) {
        int32_t grid_x = static_cast<int32_t>(i % 8u) - 4;
        int32_t grid_y = static_cast<int32_t>(i / 8u) - 4;
        world->position_x[i] = mm::fix_from_int(grid_x);
        world->position_y[i] = mm::fix_from_int(grid_y);
        world->health[i] = 100;
    }
}

bool sim_command_is_canonical(const SimCommand* command, uint32_t player_count, uint32_t unit_count) {
    if (!command || player_count == 0 || player_count > SIM_MAX_PLAYERS ||
        unit_count > SIM_MAX_UNITS || command->player_id >= player_count ||
        command->unit_index >= unit_count) return false;
    switch (command->kind) {
        case SIM_COMMAND_SET_VELOCITY:
            return command->amount == 0;
        case SIM_COMMAND_DAMAGE:
            return command->value_x == 0 && command->value_y == 0 && command->amount >= 0;
        default:
            return false;
    }
}

bool sim_validate_commands(const SimWorld* world, const SimCommandBuffer* commands) {
    if (!world || world->unit_count > SIM_MAX_UNITS) return false;
    if (!commands) return true;
    if (commands->count > SIM_MAX_COMMANDS_PER_TICK) return false;
    if (commands->count > 0 && !commands->commands) return false;

    for (uint32_t i = 0; i < commands->count; ++i) {
        if (!sim_command_is_canonical(&commands->commands[i], SIM_MAX_PLAYERS, world->unit_count))
            return false;
    }
    return true;
}

bool sim_tick(SimWorld* world, const SimCommandBuffer* commands) {
    // Validate the complete input first. No command or tick mutation happens when
    // any record is malformed, so callers can reject a packet atomically.
    if (!sim_validate_commands(world, commands)) return false;

    if (commands) {
        for (uint32_t i = 0; i < commands->count; ++i) {
            const SimCommand& command = commands->commands[i];
            uint32_t unit = command.unit_index;
            if (command.kind == SIM_COMMAND_SET_VELOCITY) {
                world->velocity_x[unit] = command.value_x;
                world->velocity_y[unit] = command.value_y;
            } else {
                if (command.amount >= world->health[unit]) world->health[unit] = 0;
                else world->health[unit] -= command.amount;
                if (command.amount > 0) world->cooldown[unit] = 3;
            }
        }
    }

    // Ascending slot order is the placeholder's deterministic schedule.
    for (uint32_t i = 0; i < world->unit_count; ++i) {
        mm::fix dx = mm::fix_mul(world->velocity_x[i], SIM_DT_FIXED);
        mm::fix dy = mm::fix_mul(world->velocity_y[i], SIM_DT_FIXED);
        world->position_x[i] = fix_add_wrap(world->position_x[i], dx);
        world->position_y[i] = fix_add_wrap(world->position_y[i], dy);
        if (world->cooldown[i] > 0) --world->cooldown[i];
    }

    (void)mm::pcg32_next(&world->rng); // exercise the serialized/hashed sim RNG stream each tick
    ++world->tick;
    return true;
}
