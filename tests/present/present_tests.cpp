// M3.3 acceptance: platform cadence drives the sim; eng_game owns only fixed
// snapshots, capture, interpolation, and the fixed->float edge.
#include "test.h"

#include "core/sim_config.h"
#include "game/present.h"
#include "platform/platform_fixed_step.h"
#include "sim/sim_hash.h"

#include <cstring>

static const size_t PRESENT_WORLD_BYTES = 128u * 1024u;
static const size_t PRESENT_ARENA_BYTES = 8u * 1024u;
alignas(16) static uint8_t g_present_world_storage[8][PRESENT_WORLD_BYTES];
alignas(16) static uint8_t g_present_arena_storage[8][PRESENT_ARENA_BYTES];

typedef struct PresentFixture {
    SimWorld world;
    Arena world_arena;
    PresentState present;
    Arena present_arena;
} PresentFixture;

static bool present_fixture_init(PresentFixture* fixture, uint32_t storage_index, uint64_t seed) {
    if (!fixture || storage_index >= 8u) return false;
    *fixture = PresentFixture{};
    arena_init_fixed(&fixture->world_arena, g_present_world_storage[storage_index],
                     PRESENT_WORLD_BYTES);
    arena_init_fixed(&fixture->present_arena, g_present_arena_storage[storage_index],
                     PRESENT_ARENA_BYTES);
    const SimWorldConfig config{128u, SIM_MAX_UNITS, SIM_DEFAULT_DAMAGE_EVENT_CAPACITY};
    return sim_init(&fixture->world, &fixture->world_arena, seed, config) &&
           present_init(&fixture->present, &fixture->present_arena, &fixture->world);
}

static bool pointer_in_arena(const void* pointer, const Arena* arena) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(arena->base);
    return value >= begin && value < begin + arena->reserved;
}

static SimCommand tick_velocity_command(const SimWorld* world) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.unit_index = 0u;
    command.value_x = mm::fix_from_int(static_cast<int32_t>(world->tick + 1u));
    return command;
}

typedef struct TickTrace {
    uint64_t generated_at[16];
    mm::fix velocity_x[16];
    uint32_t count;
} TickTrace;

static bool drive_frame(PresentFixture* fixture, PlatformFixedStep* step, double delta,
                        bool build_draws, TickTrace* trace) {
    if (!fixture || !step) return false;
    platform_fixed_step_add_frame_delta(step, delta);
    while (platform_fixed_step_tick_owed(step)) {
        SimCommand command = tick_velocity_command(&fixture->world);
        if (trace) {
            if (trace->count >= 16u) return false;
            trace->generated_at[trace->count] = fixture->world.tick;
            trace->velocity_x[trace->count] = command.value_x;
            ++trace->count;
        }
        const SimCommandBuffer buffer{&command, 1u};
        if (!sim_tick(&fixture->world, &buffer) ||
            !present_capture(&fixture->present, &fixture->world) ||
            !platform_fixed_step_consume_tick(step)) {
            return false;
        }
    }

    if (build_draws) {
        DrawItem items[SIM_MAX_UNITS]{};
        const uint32_t count = present_build_draw_items(
            &fixture->present, platform_fixed_step_alpha(step),
            MeshHandle{handle_make(1u, 1u)}, MaterialHandle{handle_make(1u, 1u)},
            items, SIM_MAX_UNITS);
        if (count != fixture->present.current.live_count) return false;
    }
    return true;
}

TEST(present, init_is_transactional_arena_backed_and_starts_from_world) {
    PresentFixture fixture{};
    CHECK(present_fixture_init(&fixture, 0u, 7u));
    CHECK(present_memory_required() <= PRESENT_ARENA_BYTES);
    CHECK(pointer_in_arena(fixture.present.previous.units, &fixture.present_arena));
    CHECK(pointer_in_arena(fixture.present.current.units, &fixture.present_arena));
    CHECK(fixture.present.previous.units != fixture.present.current.units);
    CHECK(fixture.present.previous.tick == 0u);
    CHECK(fixture.present.current.tick == 0u);
    CHECK(fixture.present.previous.live_count == SIM_MAX_UNITS);
    CHECK(fixture.present.current.live_count == SIM_MAX_UNITS);
    CHECK(std::memcmp(fixture.present.previous.units, fixture.present.current.units,
                      sizeof(RenderSnapshotUnit) * SIM_MAX_UNITS) == 0);

    alignas(16) uint8_t short_storage[PRESENT_ARENA_BYTES]{};
    Arena short_arena{};
    arena_init_fixed(&short_arena, short_storage, present_memory_required() - 1u);
    PresentState untouched{};
    untouched.previous.tick = 111u;
    untouched.current.tick = 222u;
    untouched.previous.units = reinterpret_cast<RenderSnapshotUnit*>(uintptr_t{1u});
    untouched.current.units = reinterpret_cast<RenderSnapshotUnit*>(uintptr_t{2u});
    PresentState before = untouched;
    CHECK(!present_init(&untouched, &short_arena, &fixture.world));
    CHECK(std::memcmp(&untouched, &before, sizeof(before)) == 0);
    CHECK(short_arena.offset == 0u);

    Arena invalid_arena{};
    arena_init_fixed(&invalid_arena, short_storage, sizeof(short_storage));
    CHECK(!present_init(&untouched, &invalid_arena, nullptr));
    CHECK(std::memcmp(&untouched, &before, sizeof(before)) == 0);
    CHECK(invalid_arena.offset == 0u);
}

TEST(present, extraction_is_const_atomic_slot_stable_and_hash_neutral) {
    PresentFixture fixture{};
    CHECK(present_fixture_init(&fixture, 1u, 11u));
    const uint64_t initial_hash = sim_hash_state(&fixture.world);
    CHECK(present_capture(&fixture.present, &fixture.world));
    CHECK(sim_hash_state(&fixture.world) == initial_hash);

    SimCommand command = tick_velocity_command(&fixture.world);
    const SimCommandBuffer buffer{&command, 1u};
    CHECK(sim_tick(&fixture.world, &buffer));
    const uint64_t hash_after_tick = sim_hash_state(&fixture.world);
    CHECK(present_capture(&fixture.present, &fixture.world));
    CHECK(sim_hash_state(&fixture.world) == hash_after_tick);
    CHECK(fixture.present.previous.tick == 0u);
    CHECK(fixture.present.current.tick == 1u);
    CHECK(fixture.present.current.live_count == SIM_MAX_UNITS);
    CHECK(fixture.present.current.units[0].entity.h == fixture.world.unit_entities[0].h);
    CHECK(fixture.present.current.units[63].entity.h == fixture.world.unit_entities[63].h);

    CHECK(sim_destroy_deferred(&fixture.world, fixture.world.unit_entities[10]));
    CHECK(sim_tick(&fixture.world, nullptr));
    CHECK(present_capture(&fixture.present, &fixture.world));
    CHECK(fixture.present.current.live_count == SIM_MAX_UNITS - 1u);
    CHECK(fixture.present.current.units[10].entity.h == HANDLE_NULL);
    CHECK(fixture.present.current.units[63].entity.h != HANDLE_NULL);

    RenderSnapshotUnit output_units[SIM_MAX_UNITS]{};
    RenderSnapshot output{999u, 55u, output_units};
    RenderSnapshot before = output;
    RenderSnapshotUnit bytes_before[SIM_MAX_UNITS]{};
    std::memcpy(bytes_before, output_units, sizeof(output_units));
    SimWorld invalid = fixture.world;
    invalid.unit_entities[5] = EntityId{handle_make(5u, 2u)};
    CHECK(!snapshot_extract(&invalid, &output));
    CHECK(output.tick == before.tick && output.live_count == before.live_count &&
          output.units == before.units);
    CHECK(std::memcmp(output_units, bytes_before, sizeof(output_units)) == 0);
}

TEST(present, cadence_grouping_minimize_and_catchup_preserve_hash_stream) {
    PresentFixture at_60{}, at_30{}, mixed{}, rendered{}, minimized{}, catchup{};
    CHECK(present_fixture_init(&at_60, 0u, 99u));
    CHECK(present_fixture_init(&at_30, 1u, 99u));
    CHECK(present_fixture_init(&mixed, 2u, 99u));
    CHECK(present_fixture_init(&rendered, 3u, 123u));
    CHECK(present_fixture_init(&minimized, 4u, 123u));
    CHECK(present_fixture_init(&catchup, 5u, 321u));

    PlatformFixedStep step_60{}, step_30{}, step_mixed{}, step_rendered{}, step_minimized{};
    platform_fixed_step_init(&step_60);
    platform_fixed_step_init(&step_30);
    platform_fixed_step_init(&step_mixed);
    platform_fixed_step_init(&step_rendered);
    platform_fixed_step_init(&step_minimized);
    for (uint32_t frame = 0u; frame < 60u; ++frame) {
        CHECK(drive_frame(&at_60, &step_60, SIM_DT_SECONDS * 0.5, false, nullptr));
        CHECK(drive_frame(&rendered, &step_rendered, SIM_DT_SECONDS * 0.5, true, nullptr));
        CHECK(drive_frame(&minimized, &step_minimized, SIM_DT_SECONDS * 0.5, false, nullptr));
    }
    for (uint32_t frame = 0u; frame < 30u; ++frame) {
        CHECK(drive_frame(&at_30, &step_30, SIM_DT_SECONDS, false, nullptr));
        const double delta = (frame & 1u) ? SIM_DT_SECONDS * 1.5 : SIM_DT_SECONDS * 0.5;
        CHECK(drive_frame(&mixed, &step_mixed, delta, false, nullptr));
    }

    CHECK(at_60.world.tick == 30u && at_30.world.tick == 30u && mixed.world.tick == 30u);
    CHECK(sim_hash_state(&at_60.world) == sim_hash_state(&at_30.world));
    CHECK(sim_hash_state(&at_30.world) == sim_hash_state(&mixed.world));
    CHECK(rendered.world.tick == 30u && minimized.world.tick == 30u);
    CHECK(sim_hash_state(&rendered.world) == sim_hash_state(&minimized.world));

    PlatformFixedStep step_catchup{};
    platform_fixed_step_init(&step_catchup);
    TickTrace trace{};
    CHECK(drive_frame(&catchup, &step_catchup, 1.0, false, &trace));
    CHECK(catchup.world.tick == 7u);
    CHECK(trace.count == 7u);
    CHECK_APPROX(platform_fixed_step_alpha(&step_catchup), 0.5, 1.0e-9);
}

TEST(present, multi_tick_frame_generates_fresh_commands_and_captures_each_tick) {
    PresentFixture fixture{};
    CHECK(present_fixture_init(&fixture, 6u, 55u));
    PlatformFixedStep step{};
    platform_fixed_step_init(&step);
    TickTrace trace{};
    CHECK(drive_frame(&fixture, &step, SIM_DT_SECONDS * 2.0, false, &trace));
    CHECK(trace.count == 2u);
    CHECK(trace.generated_at[0] == 0u && trace.generated_at[1] == 1u);
    CHECK(trace.velocity_x[0] == mm::fix_from_int(1));
    CHECK(trace.velocity_x[1] == mm::fix_from_int(2));
    CHECK(fixture.present.previous.tick == 1u);
    CHECK(fixture.present.current.tick == 2u);

    platform_fixed_step_add_frame_delta(&step, SIM_DT_SECONDS);
    SimCommand invalid{};
    invalid.kind = SIM_COMMAND_SET_VELOCITY;
    invalid.unit_index = static_cast<uint16_t>(SIM_MAX_UNITS);
    const SimCommandBuffer invalid_buffer{&invalid, 1u};
    const double debt_before = step.accumulator_seconds;
    const uint64_t tick_before = fixture.world.tick;
    CHECK(!sim_tick(&fixture.world, &invalid_buffer));
    CHECK(platform_fixed_step_tick_owed(&step));
    CHECK(step.accumulator_seconds == debt_before);
    CHECK(fixture.world.tick == tick_before);
}

TEST(present, interpolation_is_previous_to_current_and_identity_aware) {
    PresentFixture fixture{};
    CHECK(present_fixture_init(&fixture, 7u, 88u));
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        fixture.present.previous.units[slot] = RenderSnapshotUnit{};
        fixture.present.current.units[slot] = RenderSnapshotUnit{};
    }

    const EntityId original{handle_make(0u, 1u)};
    fixture.present.previous.live_count = 1u;
    fixture.present.current.live_count = 1u;
    fixture.present.previous.units[0] = RenderSnapshotUnit{
        original, 0, mm::fix_from_int(20), 0};
    fixture.present.current.units[0] = RenderSnapshotUnit{
        original, mm::fix_from_int(100), mm::fix_from_int(60), 0};

    DrawItem items[2]{};
    const MeshHandle mesh{handle_make(1u, 1u)};
    const MaterialHandle material{handle_make(2u, 1u)};
    CHECK(present_build_draw_items(&fixture.present, 0.0, mesh, material, items, 2u) == 1u);
    CHECK_APPROX(items[0].model.m[3][0], 0.0f, 0.01f);
    CHECK_APPROX(items[0].model.m[3][2], 20.0f, 0.01f);
    CHECK(present_build_draw_items(&fixture.present, 0.5, mesh, material, items, 2u) == 1u);
    CHECK_APPROX(items[0].model.m[3][0], 50.0f, 0.01f);
    CHECK_APPROX(items[0].model.m[3][2], 40.0f, 0.01f);
    CHECK(present_build_draw_items(&fixture.present, 0.999, mesh, material, items, 2u) == 1u);
    CHECK_APPROX(items[0].model.m[3][0], 99.9f, 0.02f);

    // Same slot, different generation: render current directly even at alpha zero.
    fixture.present.current.units[0].entity = EntityId{handle_make(0u, 2u)};
    CHECK(present_build_draw_items(&fixture.present, 0.0, mesh, material, items, 2u) == 1u);
    CHECK_APPROX(items[0].model.m[3][0], 100.0f, 0.01f);
    CHECK_APPROX(items[0].model.m[3][2], 60.0f, 0.01f);

    fixture.present.current.units[0] = RenderSnapshotUnit{};
    fixture.present.current.live_count = 0u;
    CHECK(present_build_draw_items(&fixture.present, 0.5, mesh, material, items, 2u) == 0u);
}
