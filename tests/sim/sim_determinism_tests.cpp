#include "test.h"

#include "sim/replay.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cstdio>
#include <cstring>
#include <ctime>

static const uint64_t DETERMINISM_SEED = 1;
static const uint32_t DETERMINISM_PLAYERS = 2;
static const uint64_t DETERMINISM_TICKS = 10000;
static const uint64_t MUTATION_TICK = 4321;
static const size_t DETERMINISM_REPLAY_CAPACITY =
    REPLAY_HEADER_ENCODED_SIZE + static_cast<size_t>(DETERMINISM_TICKS) *
    (REPLAY_TICK_BASE_ENCODED_SIZE +
     REPLAY_PLACEHOLDER_MAX_COMMANDS * REPLAY_COMMAND_ENCODED_SIZE);
static uint8_t g_determinism_replay[DETERMINISM_REPLAY_CAPACITY];
static const size_t DETERMINISM_WORLD_CAPACITY = 2u * 1024u * 1024u;
alignas(16) static uint8_t g_record_world_storage[DETERMINISM_WORLD_CAPACITY];
alignas(16) static uint8_t g_replay_world_storage[DETERMINISM_WORLD_CAPACITY];
alignas(16) static uint8_t g_expected_world_storage[DETERMINISM_WORLD_CAPACITY];
alignas(16) static uint8_t g_actual_world_storage[DETERMINISM_WORLD_CAPACITY];
static_assert(SIM_LOGIC_HASH == 0x46e9e287878ba88cULL,
              "logic behavior changes require a deliberate replay compatibility bump");

static bool init_determinism_world(SimWorld* world, Arena* arena, uint8_t* storage,
                                   uint64_t seed) {
    arena_init_fixed(arena, storage, DETERMINISM_WORLD_CAPACITY);
    return sim_init(world, arena, seed, sim_world_config_default());
}

static size_t record_determinism_replay(uint64_t* out_final_hash) {
    ByteWriter writer;
    byte_writer_init(&writer, g_determinism_replay, sizeof(g_determinism_replay));
    ReplayHeader header =
        replay_header_make(DETERMINISM_SEED, DETERMINISM_PLAYERS, DETERMINISM_TICKS);
    if (replay_write_header(&writer, &header) != REPLAY_STATUS_OK) return 0;

    SimWorld world{};
    Arena world_arena{};
    if (!init_determinism_world(&world, &world_arena,
                                g_record_world_storage, DETERMINISM_SEED)) return 0;
    for (uint64_t tick = 0; tick < DETERMINISM_TICKS; ++tick) {
        SimCommand commands[REPLAY_PLACEHOLDER_MAX_COMMANDS]{};
        uint32_t command_count = replay_generate_placeholder_commands(
            DETERMINISM_SEED, tick, DETERMINISM_PLAYERS, commands);
        SimCommandBuffer command_buffer{commands, command_count};
        if (!sim_tick(&world, &command_buffer)) return 0;
        if (replay_write_tick(&writer, DETERMINISM_PLAYERS, &command_buffer,
                              sim_hash_state(&world)) != REPLAY_STATUS_OK)
            return 0;
    }
    if (out_final_hash) *out_final_hash = sim_hash_state(&world);
    return byte_writer_size(&writer);
}

static bool open_determinism_replay(size_t size, ByteReader* reader, ReplayHeader* header) {
    byte_reader_init(reader, g_determinism_replay, size);
    return replay_read_header(reader, header) == REPLAY_STATUS_OK;
}

TEST(sim_determinism, recorded_hash_stream_matches_independent_replay_for_10000_ticks) {
    uint64_t recorded_final_hash = 0;
    size_t size = record_determinism_replay(&recorded_final_hash);
    CHECK(size == 134812);
    CHECK(recorded_final_hash == 0xac06a80d7f71b503ULL); // pins M3.2 canonical order + behavior
    if (size == 0) return;

    ByteReader reader;
    ReplayHeader header{};
    CHECK(open_determinism_replay(size, &reader, &header));
    SimWorld replayed{};
    Arena replayed_arena{};
    CHECK(init_determinism_world(&replayed, &replayed_arena,
                                 g_replay_world_storage, header.seed));
    uint64_t replayed_final_hash = 0;
    for (uint64_t tick = 0; tick < header.tick_count; ++tick) {
        SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
        uint32_t command_count = 0;
        uint64_t expected_hash = 0;
        CHECK(replay_read_tick(&reader, header.player_count, commands,
                               SIM_MAX_COMMANDS_PER_TICK, &command_count,
                               &expected_hash) == REPLAY_STATUS_OK);
        SimCommandBuffer command_buffer{commands, command_count};
        CHECK(sim_tick(&replayed, &command_buffer));
        replayed_final_hash = sim_hash_state(&replayed);
        if (replayed_final_hash != expected_hash) {
            std::printf("hash mismatch tick=%llu expected=0x%016llx actual=0x%016llx\n",
                        static_cast<unsigned long long>(tick),
                        static_cast<unsigned long long>(expected_hash),
                        static_cast<unsigned long long>(replayed_final_hash));
        }
        CHECK(replayed_final_hash == expected_hash);
    }
    CHECK(replay_require_end(&reader) == REPLAY_STATUS_OK);
    CHECK(replayed_final_hash == recorded_final_hash);
}

// G18 perf smoke: the 10,000-tick record+hash path must stay inside a generous
// CPU-time ceiling. This catches O(n^2)-style regressions (per-tick full-array
// rescans, unordered view rebuilds), not micro-jitter — the ceiling is ~100x the
// local Debug cost so loaded CI cannot trip it. CRT clock() is used, not the
// platform timer, so this test file keeps linking eng_sim only.
TEST(sim_determinism, ten_k_tick_record_stays_inside_cpu_time_ceiling) {
    const double CEILING_SECONDS = 10.0;
    std::clock_t t0 = std::clock();
    uint64_t final_hash = 0;
    size_t size = record_determinism_replay(&final_hash);
    std::clock_t t1 = std::clock();
    double cpu_seconds = static_cast<double>(t1 - t0) / static_cast<double>(CLOCKS_PER_SEC);
    std::printf("  10k-tick record+hash: %.3f s CPU\n", cpu_seconds);
    CHECK(size > 0);
    CHECK(final_hash == 0xac06a80d7f71b503ULL);   // oracle re-pinned in the smoke path
    CHECK(cpu_seconds < CEILING_SECONDS);
}

static const size_t CAPACITY_WORLD_BYTES = 4u * 1024u * 1024u;
alignas(16) static uint8_t g_capacity_storage_a[CAPACITY_WORLD_BYTES];
alignas(16) static uint8_t g_capacity_storage_b[CAPACITY_WORLD_BYTES];

// G32: the world at documented capacity — 16,384 entities fully populated, ticked,
// and churned through deferred destruction — must hash identically across two
// independent runs. The 10,000-tick oracle proves determinism at 64 units; this
// proves ordering, hashing, and recycle behavior at full load.
TEST(sim_determinism, full_capacity_world_orders_hashes_and_churns_deterministically) {
    SimWorldConfig config = sim_world_config_default();
    CHECK(config.max_entities == SIM_DEFAULT_MAX_ENTITIES);
    CHECK(sim_world_memory_required(config) <= CAPACITY_WORLD_BYTES);

    SimWorld a{}, b{};
    Arena arena_a{}, arena_b{};
    CHECK(init_determinism_world(&a, &arena_a, g_capacity_storage_a, 42u));
    CHECK(init_determinism_world(&b, &arena_b, g_capacity_storage_b, 42u));

    uint32_t created = 0u;
    for (;;) {
        EntityId entity_a = entity_manager_create(&a.entities);
        EntityId entity_b = entity_manager_create(&b.entities);
        if (entity_a.h == HANDLE_NULL && entity_b.h == HANDLE_NULL) break;
        CHECK(entity_a.h != HANDLE_NULL);
        CHECK(entity_b.h != HANDLE_NULL);
        CHECK(transform_pool_add(&a.transforms, entity_a, 1, 2, 3));
        CHECK(transform_pool_add(&b.transforms, entity_b, 1, 2, 3));
        CHECK(velocity_pool_add(&a.velocities, entity_a, 4, 5));
        CHECK(velocity_pool_add(&b.velocities, entity_b, 4, 5));
        CHECK(health_pool_add(&a.health, entity_a, 6, 7, 8u));
        CHECK(health_pool_add(&b.health, entity_b, 6, 7, 8u));
        ++created;
    }
    CHECK(created == SIM_DEFAULT_MAX_ENTITIES - SIM_MAX_UNITS);   // exhausted exactly

    for (uint32_t tick = 0u; tick < 128u; ++tick) {
        SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
        uint32_t command_count = replay_generate_placeholder_commands(42u, tick, 2u, commands);
        SimCommandBuffer command_buffer{commands, command_count};
        CHECK(sim_tick(&a, &command_buffer));
        CHECK(sim_tick(&b, &command_buffer));
        CHECK(sim_hash_state(&a) != 0u);
        CHECK(sim_hash_state(&a) == sim_hash_state(&b));
    }

    for (uint32_t unit = 1u; unit < SIM_MAX_UNITS; unit += 2u) {
        CHECK(sim_destroy_deferred(&a, a.unit_entities[unit]));
        CHECK(sim_destroy_deferred(&b, b.unit_entities[unit]));
    }
    for (uint32_t tick = 0u; tick < 32u; ++tick) {
        CHECK(sim_tick(&a, nullptr));
        CHECK(sim_tick(&b, nullptr));
        CHECK(sim_hash_state(&a) == sim_hash_state(&b));
    }

    uint32_t recycled = 0u;
    for (uint32_t ordinal = 0u; ordinal < 1000u; ++ordinal) {
        EntityId entity_a = entity_manager_create(&a.entities);
        EntityId entity_b = entity_manager_create(&b.entities);
        if (entity_a.h == HANDLE_NULL) break;
        CHECK(entity_b.h != HANDLE_NULL);
        CHECK(transform_pool_add(&a.transforms, entity_a, 9, 9, 9));
        CHECK(transform_pool_add(&b.transforms, entity_b, 9, 9, 9));
        ++recycled;
    }
    CHECK(recycled == 32u);                                   // exactly the destroyed slots
    for (uint32_t tick = 0u; tick < 16u; ++tick) {
        CHECK(sim_tick(&a, nullptr));
        CHECK(sim_tick(&b, nullptr));
        CHECK(sim_hash_state(&a) == sim_hash_state(&b));
    }
    std::printf("  full-capacity world: %u entities + %u recycled, hash-identical twin runs\n",
                SIM_DEFAULT_MAX_ENTITIES, recycled);
}

TEST(sim_determinism, controlled_mutation_reports_tick_4321_position_x_entity_7) {    size_t size = record_determinism_replay(nullptr);
    CHECK(size > 0);
    if (size == 0) return;

    ByteReader reader;
    ReplayHeader header{};
    CHECK(open_determinism_replay(size, &reader, &header));
    SimWorld expected{}, actual{};
    Arena expected_arena{}, actual_arena{};
    CHECK(init_determinism_world(&expected, &expected_arena,
                                 g_expected_world_storage, header.seed));
    CHECK(init_determinism_world(&actual, &actual_arena,
                                 g_actual_world_storage, header.seed));

    uint64_t first_divergent_tick = UINT64_MAX;
    SimStateDiff first_diff{};
    for (uint64_t tick = 0; tick < header.tick_count; ++tick) {
        SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
        uint32_t command_count = 0;
        uint64_t recorded_hash = 0;
        CHECK(replay_read_tick(&reader, header.player_count, commands,
                               SIM_MAX_COMMANDS_PER_TICK, &command_count,
                               &recorded_hash) == REPLAY_STATUS_OK);
        SimCommandBuffer command_buffer{commands, command_count};
        CHECK(sim_tick(&expected, &command_buffer));
        CHECK(sim_tick(&actual, &command_buffer));
        if (tick == MUTATION_TICK) {
            TransformView transform{};
            CHECK(transform_pool_get(&actual.transforms, actual.unit_entities[7], &transform));
            *transform.position_x += FIX_ONE;
        }

        uint64_t expected_hash = sim_hash_state(&expected);
        uint64_t actual_hash = sim_hash_state(&actual);
        CHECK(expected_hash == recorded_hash);
        if (expected_hash != actual_hash) {
            first_divergent_tick = tick;
            CHECK(sim_diff_state(&expected, &actual, &first_diff));
            break;
        }
    }

    std::printf("controlled divergence tick=%llu field=%s entity=%u\n",
                static_cast<unsigned long long>(first_divergent_tick),
                sim_state_field_name(first_diff.field), first_diff.index);
    CHECK(first_divergent_tick == MUTATION_TICK);
    CHECK(first_diff.field == SIM_STATE_FIELD_POSITION_X);
    CHECK(std::strcmp(sim_state_field_name(first_diff.field), "position_x") == 0);
    CHECK(first_diff.index == 7u);
    CHECK(first_diff.expected_value != first_diff.actual_value);
}
