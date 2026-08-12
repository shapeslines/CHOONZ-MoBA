#include "test.h"
#include "sim/sim.h"

#include <cstring>

static SimCommand set_velocity(uint8_t player, uint16_t unit, mm::fix x, mm::fix y) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.player_id = player;
    command.unit_index = unit;
    command.value_x = x;
    command.value_y = y;
    return command;
}

static SimCommand damage(uint8_t player, uint16_t unit, int32_t amount) {
    SimCommand command{};
    command.kind = SIM_COMMAND_DAMAGE;
    command.player_id = player;
    command.unit_index = unit;
    command.amount = amount;
    return command;
}

TEST(sim, same_seed_and_commands_are_byte_identical) {
    SimWorld a, b;
    sim_init(&a, 0x12345678ULL);
    sim_init(&b, 0x12345678ULL);
    CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);

    SimCommand commands[3] = {
        set_velocity(0, 7, mm::fix_from_int(3), mm::fix_from_int(-2)),
        damage(1, 7, 13),
        set_velocity(0, 3, mm::fix_from_int(-1), mm::fix_from_int(1)),
    };
    SimCommandBuffer buffer{commands, 3};
    for (uint32_t tick = 0; tick < 128; ++tick) {
        CHECK(sim_tick(&a, &buffer));
        CHECK(sim_tick(&b, &buffer));
        CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(a.tick == 128);
}

TEST(sim, command_order_is_literal) {
    SimWorld world;
    sim_init(&world, 7);
    mm::fix start_x = world.position_x[2];
    SimCommand commands[4] = {
        set_velocity(0, 2, mm::fix_from_int(5), 0),
        set_velocity(0, 2, mm::fix_from_int(2), 0),
        damage(0, 2, 30),
        damage(0, 2, 80),
    };
    SimCommandBuffer buffer{commands, 4};
    CHECK(sim_tick(&world, &buffer));
    CHECK(world.velocity_x[2] == mm::fix_from_int(2));
    CHECK(world.position_x[2] == start_x + mm::fix_mul(mm::fix_from_int(2), SIM_DT_FIXED));
    CHECK(world.health[2] == 0);
    CHECK(world.cooldown[2] == 2);
}

TEST(sim, malformed_buffer_has_no_partial_mutation) {
    SimWorld world;
    sim_init(&world, 99);
    SimWorld before = world;

    SimCommand commands[2] = {
        set_velocity(0, 1, mm::fix_from_int(4), 0),
        damage(0, 1, 5),
    };
    commands[1].kind = 0xff;
    SimCommandBuffer buffer{commands, 2};
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);

    buffer.commands = nullptr;
    buffer.count = 1;
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);

    commands[0] = set_velocity(static_cast<uint8_t>(SIM_MAX_PLAYERS), 1, 0, 0);
    buffer.commands = commands;
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);

    commands[0] = damage(0, static_cast<uint16_t>(SIM_MAX_UNITS), 1);
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);
}
