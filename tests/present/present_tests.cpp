// M3.3 present-glue tests — snapshot extraction, the fixed-tick accumulator, and the
// single fixed->float interpolation point (docs/slate-moba-phase3-m3.3.md, S1-S5).
// Headless: no window, no renderer; the renderer seam is only DrawItem structs.
#include "test.h"
#include "game/present.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cstring>

static const size_t PRESENT_WORLD_BYTES = 2u * 1024u * 1024u;
alignas(16) static uint8_t g_present_storage[PRESENT_WORLD_BYTES];

static bool present_world_init(SimWorld* world, Arena* arena, uint64_t seed) {
    arena_init_fixed(arena, g_present_storage, PRESENT_WORLD_BYTES);
    return sim_init(world, arena, seed, sim_world_config_default());
}

static SimCommand velocity_command(uint16_t unit, mm::fix vx, mm::fix vy) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.unit_index = unit;
    command.value_x = vx;
    command.value_y = vy;
    return command;
}

TEST(present, snapshot_extract_is_fixed_non_mutating_and_slot_ordered) {
    SimWorld world{};
    Arena arena{};
    CHECK(present_world_init(&world, &arena, 7u));

    // Units spawn on an 8x8 grid (sim.cpp); read the start position, then move.
    TransformView start{};
    CHECK(transform_pool_get(&world.transforms, world.unit_entities[3], &start));
    const mm::fix start_x = *start.position_x;
    const mm::fix start_y = *start.position_y;

    SimCommand commands[] = { velocity_command(3u, mm::fix_from_int(4), mm::fix_from_int(6)) };
    SimCommandBuffer buffer{commands, 1u};
    CHECK(sim_tick(&world, &buffer));
    uint64_t hash_after_tick = sim_hash_state(&world);

    RenderSnapshot snap{};
    CHECK(snapshot_extract(&world, &snap) == SIM_MAX_UNITS);
    CHECK(snap.tick == 1u);
    CHECK(snap.units[3].entity.h == world.unit_entities[3].h);
    CHECK(snap.units[3].x == start_x + mm::fix_mul(mm::fix_from_int(4), SIM_DT_FIXED));
    CHECK(snap.units[3].y == start_y + mm::fix_mul(mm::fix_from_int(6), SIM_DT_FIXED));
    CHECK(snap.units[0].entity.h == world.unit_entities[0].h);
    CHECK(snap.units[63].entity.h == world.unit_entities[63].h);

    CHECK(sim_hash_state(&world) == hash_after_tick);   // extraction does not mutate

    // Dead unit -> slot marked HANDLE_NULL (count stays at the last live slot).
    CHECK(sim_destroy_deferred(&world, world.unit_entities[10]));
    CHECK(sim_tick(&world, nullptr));
    CHECK(snapshot_extract(&world, &snap) == SIM_MAX_UNITS);
    CHECK(snap.units[10].entity.h == HANDLE_NULL);
    CHECK(snap.units[9].entity.h != HANDLE_NULL);
}

TEST(present, advance_runs_whole_ticks_and_clamps_catchup) {
    // S3: the accumulator owes whole 30 Hz ticks; render rate must not change the
    // sim. Three worlds, same seed, different frame splits — each covering exactly
    // one second of sim time — must run the same 30 ticks with the same hash. This
    // is the headless proof that "render rate never affects sim hashes".
    SimWorld a{}, b{}, c{};
    Arena arena_a{}, arena_b{}, arena_c{};
    CHECK(present_world_init(&a, &arena_a, 99u));
    CHECK(present_world_init(&b, &arena_b, 99u));
    CHECK(present_world_init(&c, &arena_c, 99u));
    PresentState pa{}, pb{}, pc{};
    present_init(&pa); present_init(&pb); present_init(&pc);

    uint32_t ticks_a = 0u, ticks_b = 0u, ticks_c = 0u;
    const double dt = 1.0 / 30.0;
    for (int frame = 0; frame < 60; ++frame) {                 // 60 fps
        ticks_a += present_advance(&pa, dt / 2.0, &a, nullptr);
    }
    for (int frame = 0; frame < 30; ++frame) {                 // 30 fps
        ticks_b += present_advance(&pb, dt, &b, nullptr);
    }
    for (int frame = 0; frame < 30; ++frame) {                 // mixed 60/20 fps
        double mixed = (frame % 2 == 0) ? dt / 2.0 : dt * 1.5;   // 15 x 1/60 + 15 x 1/20 = 1s
        ticks_c += present_advance(&pc, mixed, &c, nullptr);
    }
    CHECK(ticks_a == 30u && ticks_b == 30u && ticks_c == 30u);
    CHECK(a.tick == 30u && b.tick == 30u && c.tick == 30u);
    CHECK(sim_hash_state(&a) == sim_hash_state(&b));
    CHECK(sim_hash_state(&b) == sim_hash_state(&c));

    // Minimize/unfocus stall: a single huge frame delta is clamped to
    // SIM_MAX_CATCHUP_S (0.25 s) -> 7 ticks, not 30.
    PresentState stall{};
    present_init(&stall);
    CHECK(present_advance(&stall, 1.0, &a, nullptr) == 7u);
    CHECK(a.tick == 37u);
    CHECK(stall.accumulator < dt);
}

TEST(present, double_buffer_prev_is_last_tick_curr_is_latest) {
    SimWorld world{};
    Arena arena{};
    CHECK(present_world_init(&world, &arena, 5u));
    PresentState p{};
    present_init(&p);

    SimCommand commands[] = { velocity_command(1u, mm::fix_from_int(8), mm::fix_from_int(0)) };
    SimCommandBuffer buffer{commands, 1u};
    TransformView start{};
    CHECK(transform_pool_get(&world.transforms, world.unit_entities[1], &start));
    const mm::fix start_x = *start.position_x;
    CHECK(present_advance(&p, 1.0 / 30.0 * 2.0, &world, &buffer) == 2u);
    CHECK(p.prev.tick == 1u);
    CHECK(p.curr.tick == 2u);
    const mm::fix step = mm::fix_mul(mm::fix_from_int(8), SIM_DT_FIXED);
    CHECK(p.prev.units[1].x == start_x + step);              // after tick 1
    CHECK(p.curr.units[1].x == start_x + step * 2);          // after tick 2
}

TEST(present, build_draw_items_interpolates_and_converts_fixed_to_float) {
    // S4: interpolation math. Manual snapshots: unit 0 moves x: 0 -> 100 over one
    // tick; at alpha 0.5 the item must sit at 50 world units (float), y (sim) mapped
    // to world z. This is the single fixed->float owner.
    PresentState p{};
    present_init(&p);
    p.prev.tick = 1u; p.prev.count = 1u;
    p.prev.units[0] = RenderSnapshotUnit{ EntityId{handle_make(0u, 1u)}, 0, mm::fix_from_int(20), 0 };
    p.curr.tick = 2u; p.curr.count = 1u;
    p.curr.units[0] = RenderSnapshotUnit{ EntityId{handle_make(0u, 1u)}, mm::fix_from_int(100), mm::fix_from_int(60), 0 };
    p.alpha = 0.5;

    uint8_t arena_buf[64 * 1024];
    Arena arena;
    arena_init_fixed(&arena, arena_buf, sizeof(arena_buf));
    DrawItem items[8]{};
    uint32_t n = present_build_draw_items(&p, MeshHandle{handle_make(1, 1)},
                                          MaterialHandle{handle_make(1, 1)}, &arena,
                                          items, 8);
    CHECK(n == 1u);
    CHECK_APPROX(items[0].model.m[3][0], 50.0f, 0.01f);    // lerped x
    CHECK_APPROX(items[0].model.m[3][2], 40.0f, 0.01f);    // sim y -> world z, lerped
    CHECK(items[0].mesh.h == handle_make(1, 1));

    // Dead slots are skipped, capacity is honored.
    p.curr.count = 2u;
    p.curr.units[1] = RenderSnapshotUnit{};
    CHECK(present_build_draw_items(&p, MeshHandle{handle_make(1, 1)},
                                   MaterialHandle{handle_make(1, 1)}, &arena,
                                   items, 1) == 1u);
}
