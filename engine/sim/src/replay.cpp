#include "sim/replay.h"

#include "core/sim_config.h"

#include <cstring>

static ReplayStatus validate_header(const ReplayHeader& header) {
    if (header.format_version != REPLAY_FORMAT_VERSION) return REPLAY_STATUS_BAD_VERSION;
    if (header.sim_logic_hash != SIM_LOGIC_HASH) return REPLAY_STATUS_BAD_LOGIC_HASH;
    if (header.tick_rate != SIM_HZ) return REPLAY_STATUS_BAD_TICK_RATE;
    if (header.player_count == 0 || header.player_count > SIM_MAX_PLAYERS)
        return REPLAY_STATUS_BAD_PLAYER_COUNT;
    if (header.tick_count > REPLAY_MAX_TICKS) return REPLAY_STATUS_BAD_TICK_COUNT;
    return REPLAY_STATUS_OK;
}

static bool writer_has(ByteWriter* writer, size_t size) {
    if (!writer || !writer->ok || writer->offset > writer->capacity ||
        size > writer->capacity - writer->offset) {
        if (writer) writer->ok = false;
        return false;
    }
    return true;
}

static ReplayStatus reader_fail(ByteReader* reader, ReplayStatus status) {
    if (reader) reader->ok = false;
    return status;
}

ReplayHeader replay_header_make(uint64_t seed, uint32_t player_count, uint64_t tick_count) {
    ReplayHeader header{};
    header.format_version = REPLAY_FORMAT_VERSION;
    header.sim_logic_hash = SIM_LOGIC_HASH;
    header.seed = seed;
    header.tick_rate = SIM_HZ;
    header.player_count = player_count;
    header.tick_count = tick_count;
    return header;
}

ReplayStatus replay_write_header(ByteWriter* writer, const ReplayHeader* header) {
    if (!header) return REPLAY_STATUS_OVERFLOW;
    ReplayStatus status = validate_header(*header);
    if (status != REPLAY_STATUS_OK) return status;
    if (!writer_has(writer, REPLAY_HEADER_ENCODED_SIZE)) return REPLAY_STATUS_OVERFLOW;

    byte_writer_write_bytes(writer, REPLAY_MAGIC, sizeof(REPLAY_MAGIC));
    byte_writer_write_u32(writer, header->format_version);
    byte_writer_write_u64(writer, header->sim_logic_hash);
    byte_writer_write_u64(writer, header->seed);
    byte_writer_write_u32(writer, header->tick_rate);
    byte_writer_write_u32(writer, header->player_count);
    byte_writer_write_u64(writer, header->tick_count);
    return byte_writer_ok(writer) ? REPLAY_STATUS_OK : REPLAY_STATUS_OVERFLOW;
}

ReplayStatus replay_read_header(ByteReader* reader, ReplayHeader* out_header) {
    if (!reader || !out_header || !reader->ok) return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
    ByteReader cursor = *reader;
    uint8_t magic[8];
    ReplayHeader header{};
    if (!byte_reader_read_bytes(&cursor, magic, sizeof(magic)) ||
        !byte_reader_read_u32(&cursor, &header.format_version) ||
        !byte_reader_read_u64(&cursor, &header.sim_logic_hash) ||
        !byte_reader_read_u64(&cursor, &header.seed) ||
        !byte_reader_read_u32(&cursor, &header.tick_rate) ||
        !byte_reader_read_u32(&cursor, &header.player_count) ||
        !byte_reader_read_u64(&cursor, &header.tick_count))
        return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
    if (std::memcmp(magic, REPLAY_MAGIC, sizeof(magic)) != 0)
        return reader_fail(reader, REPLAY_STATUS_BAD_MAGIC);
    ReplayStatus status = validate_header(header);
    if (status != REPLAY_STATUS_OK) return reader_fail(reader, status);

    // Every tick requires at least count + hash bytes. Reject impossible lengths
    // before a caller enters a potentially large verification loop.
    uint64_t remaining = static_cast<uint64_t>(byte_reader_remaining(&cursor));
    if (header.tick_count > remaining / REPLAY_TICK_BASE_ENCODED_SIZE)
        return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
    *out_header = header;
    *reader = cursor;
    return REPLAY_STATUS_OK;
}

ReplayStatus replay_write_tick(ByteWriter* writer, uint32_t player_count,
                               const SimCommandBuffer* commands, uint64_t expected_hash) {
    if (player_count == 0 || player_count > SIM_MAX_PLAYERS) return REPLAY_STATUS_BAD_PLAYER_COUNT;
    uint32_t count = commands ? commands->count : 0;
    const SimCommand* data = commands ? commands->commands : nullptr;
    if (count > SIM_MAX_COMMANDS_PER_TICK) return REPLAY_STATUS_BAD_COMMAND_COUNT;
    if (count > 0 && !data) return REPLAY_STATUS_BAD_COMMAND;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sim_command_is_canonical(&data[i], player_count, SIM_MAX_UNITS))
            return REPLAY_STATUS_BAD_COMMAND;
    }

    size_t needed = REPLAY_TICK_BASE_ENCODED_SIZE + static_cast<size_t>(count) * REPLAY_COMMAND_ENCODED_SIZE;
    if (!writer_has(writer, needed)) return REPLAY_STATUS_OVERFLOW;
    byte_writer_write_u32(writer, count);
    byte_writer_write_u64(writer, expected_hash);
    for (uint32_t i = 0; i < count; ++i) {
        byte_writer_write_u8(writer, data[i].kind);
        byte_writer_write_u8(writer, data[i].player_id);
        byte_writer_write_u16(writer, data[i].unit_index);
        byte_writer_write_i32(writer, data[i].value_x);
        byte_writer_write_i32(writer, data[i].value_y);
        byte_writer_write_i32(writer, data[i].amount);
    }
    return byte_writer_ok(writer) ? REPLAY_STATUS_OK : REPLAY_STATUS_OVERFLOW;
}

ReplayStatus replay_read_tick(ByteReader* reader, uint32_t player_count,
                              SimCommand* out_commands, uint32_t command_capacity,
                              uint32_t* out_command_count, uint64_t* out_expected_hash) {
    if (!reader || !reader->ok || !out_command_count || !out_expected_hash)
        return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
    if (player_count == 0 || player_count > SIM_MAX_PLAYERS)
        return reader_fail(reader, REPLAY_STATUS_BAD_PLAYER_COUNT);

    ByteReader cursor = *reader;
    uint32_t count = 0;
    uint64_t expected_hash = 0;
    if (!byte_reader_read_u32(&cursor, &count) || !byte_reader_read_u64(&cursor, &expected_hash))
        return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
    if (count > SIM_MAX_COMMANDS_PER_TICK)
        return reader_fail(reader, REPLAY_STATUS_BAD_COMMAND_COUNT);
    if (count > command_capacity || (count > 0 && !out_commands))
        return reader_fail(reader, REPLAY_STATUS_OVERFLOW);

    SimCommand scratch[SIM_MAX_COMMANDS_PER_TICK]{};
    for (uint32_t i = 0; i < count; ++i) {
        if (!byte_reader_read_u8(&cursor, &scratch[i].kind) ||
            !byte_reader_read_u8(&cursor, &scratch[i].player_id) ||
            !byte_reader_read_u16(&cursor, &scratch[i].unit_index) ||
            !byte_reader_read_i32(&cursor, &scratch[i].value_x) ||
            !byte_reader_read_i32(&cursor, &scratch[i].value_y) ||
            !byte_reader_read_i32(&cursor, &scratch[i].amount))
            return reader_fail(reader, REPLAY_STATUS_TRUNCATED);
        if (!sim_command_is_canonical(&scratch[i], player_count, SIM_MAX_UNITS))
            return reader_fail(reader, REPLAY_STATUS_BAD_COMMAND);
    }

    if (count > 0) std::memcpy(out_commands, scratch, sizeof(SimCommand) * count);
    *out_command_count = count;
    *out_expected_hash = expected_hash;
    *reader = cursor;
    return REPLAY_STATUS_OK;
}

ReplayStatus replay_require_end(const ByteReader* reader) {
    if (!reader || !reader->ok) return REPLAY_STATUS_TRUNCATED;
    return byte_reader_remaining(reader) == 0 ? REPLAY_STATUS_OK : REPLAY_STATUS_TRAILING_BYTES;
}

const char* replay_status_string(ReplayStatus status) {
    switch (status) {
        case REPLAY_STATUS_OK: return "ok";
        case REPLAY_STATUS_TRUNCATED: return "truncated";
        case REPLAY_STATUS_BAD_MAGIC: return "bad magic";
        case REPLAY_STATUS_BAD_VERSION: return "unsupported format version";
        case REPLAY_STATUS_BAD_LOGIC_HASH: return "incompatible simulation logic hash";
        case REPLAY_STATUS_BAD_TICK_RATE: return "incompatible tick rate";
        case REPLAY_STATUS_BAD_PLAYER_COUNT: return "invalid player count";
        case REPLAY_STATUS_BAD_TICK_COUNT: return "invalid tick count";
        case REPLAY_STATUS_BAD_COMMAND_COUNT: return "invalid command count";
        case REPLAY_STATUS_BAD_COMMAND: return "invalid command";
        case REPLAY_STATUS_OVERFLOW: return "buffer overflow";
        case REPLAY_STATUS_TRAILING_BYTES: return "trailing bytes";
        default: return "unknown replay error";
    }
}
