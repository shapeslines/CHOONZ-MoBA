#include "test.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cstring>

static const size_t TEST_WORLD_BYTES = 8192u;
static const size_t DEFAULT_WORLD_BYTES = 2u * 1024u * 1024u;
alignas(16) static uint8_t g_default_world_storage[DEFAULT_WORLD_BYTES];

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

static SimWorldConfig test_config(uint32_t initial_units = SIM_MAX_UNITS) {
    return SimWorldConfig{SIM_MAX_UNITS, initial_units, 8u};
}

TEST(sim, initialization_sizes_the_arena_and_is_atomic) {
    SimWorldConfig defaults = sim_world_config_default();
    CHECK(defaults.max_entities == SIM_DEFAULT_MAX_ENTITIES);
    CHECK(defaults.initial_unit_count == SIM_MAX_UNITS);
    CHECK(defaults.damage_event_capacity == SIM_DEFAULT_DAMAGE_EVENT_CAPACITY);
    CHECK(sim_world_memory_required(defaults) > sim_world_memory_required(test_config()));

    size_t required = sim_world_memory_required(test_config(4u));
    CHECK(required > 0u);
    CHECK(required <= TEST_WORLD_BYTES);
    alignas(16) uint8_t exact_storage[TEST_WORLD_BYTES]{};
    Arena exact_arena;
    arena_init_fixed(&exact_arena, exact_storage, required);
    SimWorld initialized{};
    CHECK(sim_init(&initialized, &exact_arena, 9u, test_config(4u)));
    CHECK(exact_arena.offset <= required);
    CHECK(initialized.config.max_entities == SIM_MAX_UNITS);
    CHECK(initialized.config.initial_unit_count == 4u);
    CHECK(initialized.config.damage_event_capacity == 8u);
    CHECK(damage_event_queue_is_valid(&initialized.damage_events));
    CHECK(initialized.damage_events.capacity == 8u);
    CHECK(initialized.entities.live_count == 4u);

    alignas(16) uint8_t short_storage[TEST_WORLD_BYTES]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, required - 1u);
    SimWorld untouched{};
    untouched.tick = 77u;
    SimWorld before = untouched;
    CHECK(!sim_init(&untouched, &short_arena, 9u, test_config(4u)));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
    CHECK(short_arena.offset == 0u);

    const SimWorldConfig invalid[] = {
        {0u, 0u, 8u},
        {HANDLE_INDEX_MASK + 2u, 0u, 8u},
        {4u, 5u, 8u},
        {SIM_DEFAULT_MAX_ENTITIES, SIM_MAX_UNITS + 1u, 8u},
        {SIM_DEFAULT_MAX_ENTITIES, SIM_MAX_UNITS, 0u},
    };
    for (const SimWorldConfig config : invalid) {
        CHECK(sim_world_memory_required(config) == 0u);
        CHECK(!sim_init(&untouched, &short_arena, 9u, config));
        CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
        CHECK(short_arena.offset == 0u);
    }

    alignas(16) uint8_t empty_storage[TEST_WORLD_BYTES]{};
    Arena empty_arena;
    arena_init_fixed(&empty_arena, empty_storage, sizeof(empty_storage));
    SimWorld empty{};
    CHECK(sim_init(&empty, &empty_arena, 1u, test_config(0u)));
    CHECK(empty.entities.live_count == 0u);
    CHECK(empty.transforms.membership.count == 0u);
    CHECK(empty.velocities.membership.count == 0u);
    CHECK(empty.health.membership.count == 0u);
}

TEST(sim, default_units_preserve_the_placeholder_grid_and_values) {
    Arena arena;
    SimWorldConfig config = sim_world_config_default();
    CHECK(sim_world_memory_required(config) <= sizeof(g_default_world_storage));
    arena_init_fixed(&arena, g_default_world_storage, sizeof(g_default_world_storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 123u, config));
    CHECK(world.entities.capacity == SIM_DEFAULT_MAX_ENTITIES);
    CHECK(world.entities.live_count == SIM_MAX_UNITS);

    for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit) {
        EntityId entity = world.unit_entities[unit];
        CHECK(entity.h == handle_make(unit, 1u));
        CHECK(entity_manager_is_alive(&world.entities, entity));
        TransformView transform{};
        VelocityView velocity{};
        HealthView health{};
        CHECK(transform_pool_get(&world.transforms, entity, &transform));
        CHECK(velocity_pool_get(&world.velocities, entity, &velocity));
        CHECK(health_pool_get(&world.health, entity, &health));
        CHECK(*transform.position_x == mm::fix_from_int(static_cast<int32_t>(unit % 8u) - 4));
        CHECK(*transform.position_y == mm::fix_from_int(static_cast<int32_t>(unit / 8u) - 4));
        CHECK(*transform.facing == 0);
        CHECK(*velocity.velocity_x == 0);
        CHECK(*velocity.velocity_y == 0);
        CHECK(*health.current == 100);
        CHECK(*health.maximum == 100);
        CHECK(*health.damage_cooldown == 0u);
    }
}

TEST(sim, same_seed_and_commands_are_semantically_identical) {
    alignas(16) uint8_t storage_a[TEST_WORLD_BYTES]{};
    alignas(16) uint8_t storage_b[TEST_WORLD_BYTES]{};
    Arena arena_a, arena_b;
    arena_init_fixed(&arena_a, storage_a, sizeof(storage_a));
    arena_init_fixed(&arena_b, storage_b, sizeof(storage_b));
    SimWorld a{}, b{};
    CHECK(sim_init(&a, &arena_a, 0x12345678ULL, test_config()));
    CHECK(sim_init(&b, &arena_b, 0x12345678ULL, test_config()));
    CHECK(sim_hash_state(&a) == sim_hash_state(&b));
    CHECK(arena_a.offset == arena_b.offset);
    CHECK(std::memcmp(storage_a, storage_b, arena_a.offset) == 0);

    SimCommand commands[3] = {
        set_velocity(0, 7, mm::fix_from_int(3), mm::fix_from_int(-2)),
        damage(1, 7, 13),
        set_velocity(0, 3, mm::fix_from_int(-1), mm::fix_from_int(1)),
    };
    SimCommandBuffer buffer{commands, 3};
    for (uint32_t tick = 0; tick < 128u; ++tick) {
        CHECK(sim_tick(&a, &buffer));
        CHECK(sim_tick(&b, &buffer));
        CHECK(sim_hash_state(&a) == sim_hash_state(&b));
        CHECK(std::memcmp(storage_a, storage_b, arena_a.offset) == 0);
    }
    CHECK(a.tick == 128u);
    SimStateDiff diff{};
    CHECK(!sim_diff_state(&a, &b, &diff));
}

TEST(sim, command_order_and_slot_integration_are_literal) {
    alignas(16) uint8_t storage[TEST_WORLD_BYTES]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 7u, test_config()));
    EntityId entity = world.unit_entities[2];
    TransformView before{};
    CHECK(transform_pool_get(&world.transforms, entity, &before));
    mm::fix start_x = *before.position_x;
    SimCommand commands[4] = {
        set_velocity(0, 2, mm::fix_from_int(5), 0),
        set_velocity(0, 2, mm::fix_from_int(2), 0),
        damage(0, 2, 30),
        damage(0, 2, 80),
    };
    SimCommandBuffer buffer{commands, 4};
    CHECK(sim_tick(&world, &buffer));

    TransformView transform{};
    VelocityView velocity{};
    HealthView health{};
    CHECK(transform_pool_get(&world.transforms, entity, &transform));
    CHECK(velocity_pool_get(&world.velocities, entity, &velocity));
    CHECK(health_pool_get(&world.health, entity, &health));
    CHECK(*velocity.velocity_x == mm::fix_from_int(2));
    CHECK(*transform.position_x == start_x + mm::fix_mul(mm::fix_from_int(2), SIM_DT_FIXED));
    CHECK(*health.current == 0);
    CHECK(*health.damage_cooldown == 2u);
}

TEST(sim, malformed_or_unresolved_commands_have_no_partial_mutation) {
    alignas(16) uint8_t storage[TEST_WORLD_BYTES]{};
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 99u, test_config(4u)));

    SimCommand commands[2] = {
        set_velocity(0, 1, mm::fix_from_int(4), 0),
        damage(0, 1, 5),
    };
    commands[1].kind = 0xff;
    SimCommandBuffer buffer{commands, 2};
    SimWorld before = world;
    uint8_t bytes_before[TEST_WORLD_BYTES];
    std::memcpy(bytes_before, storage, sizeof(storage));
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    buffer.commands = nullptr;
    buffer.count = 1u;
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    commands[0] = set_velocity(static_cast<uint8_t>(SIM_MAX_PLAYERS), 1, 0, 0);
    buffer.commands = commands;
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    commands[0] = damage(0, 7u, 1);
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);

    CHECK(velocity_pool_remove(&world.velocities, world.unit_entities[1]));
    before = world;
    std::memcpy(bytes_before, storage, sizeof(storage));
    buffer.count = 0u;
    CHECK(!sim_tick(&world, &buffer));
    CHECK(std::memcmp(&world, &before, sizeof(world)) == 0);
    CHECK(std::memcmp(storage, bytes_before, sizeof(storage)) == 0);
}
