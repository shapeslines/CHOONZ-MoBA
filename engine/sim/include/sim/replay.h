#pragma once

#include <stddef.h>
#include <stdint.h>

#include "serialize/byte_io.h"
#include "sim/sim.h"

static const uint8_t REPLAY_MAGIC[8] = {'M', 'O', 'B', 'A', 'R', 'P', 'L', 'Y'};
static const uint32_t REPLAY_FORMAT_VERSION = 1;

// Manually reviewed compatibility key. Bump whenever deterministic behavior or
// command encoding changes. REPLAY_FORMAT_VERSION changes only when the container
// layout changes. This separation makes incompatibility deliberate and visible.
static const uint64_t SIM_LOGIC_HASH = 0xf1b4e2b29b1e9643ULL;

static const uint64_t REPLAY_MAX_TICKS = 1000000ULL; // >9 hours at 30 Hz
static const size_t REPLAY_HEADER_ENCODED_SIZE = 44;
static const size_t REPLAY_TICK_BASE_ENCODED_SIZE = 12;
static const size_t REPLAY_COMMAND_ENCODED_SIZE = 16;
static const uint32_t REPLAY_PLACEHOLDER_MAX_COMMANDS = 2;

typedef struct ReplayHeader {
    uint32_t format_version;
    uint64_t sim_logic_hash;
    uint64_t seed;
    uint32_t tick_rate;
    uint32_t player_count;
    uint64_t tick_count;
} ReplayHeader;

typedef enum ReplayStatus : uint8_t {
    REPLAY_STATUS_OK = 0,
    REPLAY_STATUS_TRUNCATED,
    REPLAY_STATUS_BAD_MAGIC,
    REPLAY_STATUS_BAD_VERSION,
    REPLAY_STATUS_BAD_LOGIC_HASH,
    REPLAY_STATUS_BAD_TICK_RATE,
    REPLAY_STATUS_BAD_PLAYER_COUNT,
    REPLAY_STATUS_BAD_TICK_COUNT,
    REPLAY_STATUS_BAD_COMMAND_COUNT,
    REPLAY_STATUS_BAD_COMMAND,
    REPLAY_STATUS_OVERFLOW,
    REPLAY_STATUS_TRAILING_BYTES,
} ReplayStatus;

ReplayHeader replay_header_make(uint64_t seed, uint32_t player_count, uint64_t tick_count);
uint32_t replay_generate_placeholder_commands(uint64_t seed, uint64_t tick,
                                              uint32_t player_count,
                                              SimCommand* out_commands);
ReplayStatus replay_write_header(ByteWriter* writer, const ReplayHeader* header);
ReplayStatus replay_read_header(ByteReader* reader, ReplayHeader* out_header);
ReplayStatus replay_write_tick(ByteWriter* writer, uint32_t player_count,
                               const SimCommandBuffer* commands, uint64_t expected_hash);
ReplayStatus replay_read_tick(ByteReader* reader, uint32_t player_count,
                              SimCommand* out_commands, uint32_t command_capacity,
                              uint32_t* out_command_count, uint64_t* out_expected_hash);
ReplayStatus replay_require_end(const ByteReader* reader);
const char* replay_status_string(ReplayStatus status);
