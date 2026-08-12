#include "test.h"

#include "sim/replay.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cstdio>
#include <cstring>

static const uint64_t DETERMINISM_SEED = 1;
static const uint32_t DETERMINISM_PLAYERS = 2;
static const uint64_t DETERMINISM_TICKS = 10000;
static const uint64_t MUTATION_TICK = 4321;
static const size_t DETERMINISM_REPLAY_CAPACITY =
    REPLAY_HEADER_ENCODED_SIZE + static_cast<size_t>(DETERMINISM_TICKS) *
    (REPLAY_TICK_BASE_ENCODED_SIZE +
     REPLAY_PLACEHOLDER_MAX_COMMANDS * REPLAY_COMMAND_ENCODED_SIZE);
static uint8_t g_determinism_replay[DETERMINISM_REPLAY_CAPACITY];
static_assert(SIM_LOGIC_HASH == 0xf1b4e2b29b1e9643ULL,
              "logic behavior changes require a deliberate replay compatibility bump");

static size_t record_determinism_replay(uint64_t* out_final_hash) {
    ByteWriter writer;
    byte_writer_init(&writer, g_determinism_replay, sizeof(g_determinism_replay));
    ReplayHeader header =
        replay_header_make(DETERMINISM_SEED, DETERMINISM_PLAYERS, DETERMINISM_TICKS);
    if (replay_write_header(&writer, &header) != REPLAY_STATUS_OK) return 0;

    SimWorld world;
    sim_init(&world, DETERMINISM_SEED);
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
    CHECK(recorded_final_hash == 0xb85d4b632571948cULL);
    if (size == 0) return;

    ByteReader reader;
    ReplayHeader header{};
    CHECK(open_determinism_replay(size, &reader, &header));
    SimWorld replayed;
    sim_init(&replayed, header.seed);
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

TEST(sim_determinism, controlled_mutation_reports_tick_4321_position_x_unit_7) {
    size_t size = record_determinism_replay(nullptr);
    CHECK(size > 0);
    if (size == 0) return;

    ByteReader reader;
    ReplayHeader header{};
    CHECK(open_determinism_replay(size, &reader, &header));
    SimWorld expected, actual;
    sim_init(&expected, header.seed);
    sim_init(&actual, header.seed);

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
        if (tick == MUTATION_TICK) actual.position_x[7] += FIX_ONE;

        uint64_t expected_hash = sim_hash_state(&expected);
        uint64_t actual_hash = sim_hash_state(&actual);
        CHECK(expected_hash == recorded_hash);
        if (expected_hash != actual_hash) {
            first_divergent_tick = tick;
            CHECK(sim_diff_state(&expected, &actual, &first_diff));
            break;
        }
    }

    std::printf("controlled divergence tick=%llu field=%s unit=%u\n",
                static_cast<unsigned long long>(first_divergent_tick),
                sim_state_field_name(first_diff.field), first_diff.unit_index);
    CHECK(first_divergent_tick == MUTATION_TICK);
    CHECK(first_diff.field == SIM_STATE_FIELD_POSITION_X);
    CHECK(std::strcmp(sim_state_field_name(first_diff.field), "position_x") == 0);
    CHECK(first_diff.unit_index == 7);
    CHECK(first_diff.expected_value != first_diff.actual_value);
}
