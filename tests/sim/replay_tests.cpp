#include "test.h"

#include "sim/replay.h"

#include <cstring>

static SimCommand replay_velocity(uint8_t player, uint16_t unit, mm::fix x, mm::fix y) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.player_id = player;
    command.unit_index = unit;
    command.value_x = x;
    command.value_y = y;
    return command;
}

static SimCommand replay_damage(uint8_t player, uint16_t unit, int32_t amount) {
    SimCommand command{};
    command.kind = SIM_COMMAND_DAMAGE;
    command.player_id = player;
    command.unit_index = unit;
    command.amount = amount;
    return command;
}

static size_t write_sample_replay(uint8_t* bytes, size_t capacity) {
    ByteWriter writer;
    byte_writer_init(&writer, bytes, capacity);
    ReplayHeader header = replay_header_make(0x0123456789abcdefULL, 2, 2);
    if (replay_write_header(&writer, &header) != REPLAY_STATUS_OK) return 0;

    SimCommand first_commands[2] = {
        replay_velocity(0, 7, mm::fix_from_int(3), mm::fix_from_int(-2)),
        replay_damage(1, 7, 13),
    };
    SimCommandBuffer first{first_commands, 2};
    SimCommandBuffer second{nullptr, 0};
    if (replay_write_tick(&writer, 2, &first, 0x1122334455667788ULL) != REPLAY_STATUS_OK)
        return 0;
    if (replay_write_tick(&writer, 2, &second, 0x8877665544332211ULL) != REPLAY_STATUS_OK)
        return 0;
    return byte_writer_size(&writer);
}

static void write_u32_le(uint8_t* at, uint32_t value) {
    at[0] = static_cast<uint8_t>(value);
    at[1] = static_cast<uint8_t>(value >> 8);
    at[2] = static_cast<uint8_t>(value >> 16);
    at[3] = static_cast<uint8_t>(value >> 24);
}

static void write_u64_le(uint8_t* at, uint64_t value) {
    for (uint32_t i = 0; i < 8; ++i) at[i] = static_cast<uint8_t>(value >> (i * 8));
}

TEST(sim, replay_memory_roundtrip_is_byte_exact) {
    uint8_t bytes[256] = {};
    size_t size = write_sample_replay(bytes, sizeof(bytes));
    CHECK(size == REPLAY_HEADER_ENCODED_SIZE + 2 * REPLAY_TICK_BASE_ENCODED_SIZE +
                      2 * REPLAY_COMMAND_ENCODED_SIZE);
    CHECK(std::memcmp(bytes, REPLAY_MAGIC, sizeof(REPLAY_MAGIC)) == 0);
    CHECK(bytes[8] == 1 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0);

    ByteReader reader;
    byte_reader_init(&reader, bytes, size);
    ReplayHeader header{};
    CHECK(replay_read_header(&reader, &header) == REPLAY_STATUS_OK);
    CHECK(header.seed == 0x0123456789abcdefULL);
    CHECK(header.tick_rate == 30);
    CHECK(header.player_count == 2);
    CHECK(header.tick_count == 2);

    SimCommand decoded[SIM_MAX_COMMANDS_PER_TICK]{};
    uint32_t count = 0;
    uint64_t hash = 0;
    CHECK(replay_read_tick(&reader, header.player_count, decoded, SIM_MAX_COMMANDS_PER_TICK,
                           &count, &hash) == REPLAY_STATUS_OK);
    CHECK(count == 2 && hash == 0x1122334455667788ULL);
    CHECK(decoded[0].kind == SIM_COMMAND_SET_VELOCITY);
    CHECK(decoded[0].player_id == 0 && decoded[0].unit_index == 7);
    CHECK(decoded[0].value_x == mm::fix_from_int(3));
    CHECK(decoded[0].value_y == mm::fix_from_int(-2));
    CHECK(decoded[1].kind == SIM_COMMAND_DAMAGE && decoded[1].amount == 13);

    CHECK(replay_read_tick(&reader, header.player_count, decoded, SIM_MAX_COMMANDS_PER_TICK,
                           &count, &hash) == REPLAY_STATUS_OK);
    CHECK(count == 0 && hash == 0x8877665544332211ULL);
    CHECK(replay_require_end(&reader) == REPLAY_STATUS_OK);

    uint8_t encoded_again[256] = {};
    ByteWriter writer;
    byte_writer_init(&writer, encoded_again, sizeof(encoded_again));
    CHECK(replay_write_header(&writer, &header) == REPLAY_STATUS_OK);
    SimCommand first_commands[2] = {decoded[0], decoded[1]};
    first_commands[0] = replay_velocity(0, 7, mm::fix_from_int(3), mm::fix_from_int(-2));
    first_commands[1] = replay_damage(1, 7, 13);
    SimCommandBuffer first{first_commands, 2};
    SimCommandBuffer second{nullptr, 0};
    CHECK(replay_write_tick(&writer, 2, &first, 0x1122334455667788ULL) == REPLAY_STATUS_OK);
    CHECK(replay_write_tick(&writer, 2, &second, 0x8877665544332211ULL) == REPLAY_STATUS_OK);
    CHECK(byte_writer_size(&writer) == size);
    CHECK(std::memcmp(bytes, encoded_again, size) == 0);
}

TEST(sim, replay_header_rejects_incompatible_and_impossible_values) {
    uint8_t original[256] = {};
    size_t size = write_sample_replay(original, sizeof(original));
    CHECK(size > 0);
    uint8_t mutated[256] = {};

    struct HeaderCase {
        size_t offset;
        uint64_t value;
        size_t width;
        ReplayStatus expected;
    };
    const HeaderCase cases[] = {
        {8, 2, 4, REPLAY_STATUS_BAD_VERSION},
        {12, SIM_LOGIC_HASH ^ 1ULL, 8, REPLAY_STATUS_BAD_LOGIC_HASH},
        {28, 60, 4, REPLAY_STATUS_BAD_TICK_RATE},
        {32, 0, 4, REPLAY_STATUS_BAD_PLAYER_COUNT},
        {32, SIM_MAX_PLAYERS + 1ULL, 4, REPLAY_STATUS_BAD_PLAYER_COUNT},
        {36, REPLAY_MAX_TICKS + 1ULL, 8, REPLAY_STATUS_BAD_TICK_COUNT},
    };
    for (const HeaderCase& test_case : cases) {
        std::memcpy(mutated, original, size);
        if (test_case.width == 4)
            write_u32_le(mutated + test_case.offset, static_cast<uint32_t>(test_case.value));
        else
            write_u64_le(mutated + test_case.offset, test_case.value);
        ByteReader reader;
        byte_reader_init(&reader, mutated, size);
        ReplayHeader header{};
        CHECK(replay_read_header(&reader, &header) == test_case.expected);
        CHECK(!byte_reader_ok(&reader));
        CHECK(reader.offset == 0);
    }

    std::memcpy(mutated, original, size);
    mutated[0] ^= 1;
    ByteReader bad_magic;
    byte_reader_init(&bad_magic, mutated, size);
    ReplayHeader header{};
    CHECK(replay_read_header(&bad_magic, &header) == REPLAY_STATUS_BAD_MAGIC);

    ByteReader truncated;
    byte_reader_init(&truncated, original, REPLAY_HEADER_ENCODED_SIZE - 1);
    CHECK(replay_read_header(&truncated, &header) == REPLAY_STATUS_TRUNCATED);

    uint8_t impossible[REPLAY_HEADER_ENCODED_SIZE + REPLAY_TICK_BASE_ENCODED_SIZE] = {};
    std::memcpy(impossible, original, sizeof(impossible));
    write_u64_le(impossible + 36, 2);
    ByteReader too_short;
    byte_reader_init(&too_short, impossible, sizeof(impossible));
    CHECK(replay_read_header(&too_short, &header) == REPLAY_STATUS_TRUNCATED);
}

TEST(sim, replay_tick_rejects_malformed_commands_without_partial_output) {
    uint8_t original[256] = {};
    size_t size = write_sample_replay(original, sizeof(original));
    CHECK(size > 0);
    const size_t command_offset = REPLAY_HEADER_ENCODED_SIZE + REPLAY_TICK_BASE_ENCODED_SIZE;

    struct CommandCase {
        size_t offset;
        uint8_t value;
    };
    const CommandCase cases[] = {
        {command_offset, 0xff},
        {command_offset + 1, static_cast<uint8_t>(SIM_MAX_PLAYERS)},
        {command_offset + 2, static_cast<uint8_t>(SIM_MAX_UNITS)},
        {command_offset + 12, 1}, // SET_VELOCITY must carry zero damage amount.
    };

    for (const CommandCase& test_case : cases) {
        uint8_t mutated[256] = {};
        std::memcpy(mutated, original, size);
        mutated[test_case.offset] = test_case.value;
        ByteReader reader;
        byte_reader_init(&reader, mutated, size);
        ReplayHeader header{};
        CHECK(replay_read_header(&reader, &header) == REPLAY_STATUS_OK);
        size_t tick_offset = reader.offset;
        SimCommand commands[2];
        std::memset(commands, 0x5a, sizeof(commands));
        SimCommand before[2];
        std::memcpy(before, commands, sizeof(before));
        uint32_t count = 77;
        uint64_t hash = 88;
        CHECK(replay_read_tick(&reader, header.player_count, commands, 2, &count, &hash) ==
              REPLAY_STATUS_BAD_COMMAND);
        CHECK(reader.offset == tick_offset);
        CHECK(std::memcmp(commands, before, sizeof(commands)) == 0);
        CHECK(count == 77 && hash == 88);
    }

    uint8_t bad_count[256] = {};
    std::memcpy(bad_count, original, size);
    write_u32_le(bad_count + REPLAY_HEADER_ENCODED_SIZE, SIM_MAX_COMMANDS_PER_TICK + 1);
    ByteReader reader;
    byte_reader_init(&reader, bad_count, size);
    ReplayHeader header{};
    CHECK(replay_read_header(&reader, &header) == REPLAY_STATUS_OK);
    SimCommand command{};
    uint32_t count = 0;
    uint64_t hash = 0;
    CHECK(replay_read_tick(&reader, header.player_count, &command, 1, &count, &hash) ==
          REPLAY_STATUS_BAD_COMMAND_COUNT);
}

TEST(sim, replay_bounds_and_trailing_bytes_are_rejected) {
    uint8_t bytes[256] = {};
    size_t size = write_sample_replay(bytes, sizeof(bytes));
    CHECK(size > 0);

    ByteReader truncated;
    byte_reader_init(&truncated, bytes, size - 1);
    ReplayHeader header{};
    CHECK(replay_read_header(&truncated, &header) == REPLAY_STATUS_OK);
    SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
    uint32_t count = 0;
    uint64_t hash = 0;
    CHECK(replay_read_tick(&truncated, header.player_count, commands, SIM_MAX_COMMANDS_PER_TICK,
                           &count, &hash) == REPLAY_STATUS_OK);
    CHECK(replay_read_tick(&truncated, header.player_count, commands, SIM_MAX_COMMANDS_PER_TICK,
                           &count, &hash) == REPLAY_STATUS_TRUNCATED);

    uint8_t with_trailing[257] = {};
    std::memcpy(with_trailing, bytes, size);
    with_trailing[size] = 0xcc;
    ByteReader trailing;
    byte_reader_init(&trailing, with_trailing, size + 1);
    CHECK(replay_read_header(&trailing, &header) == REPLAY_STATUS_OK);
    for (uint64_t tick = 0; tick < header.tick_count; ++tick) {
        CHECK(replay_read_tick(&trailing, header.player_count, commands,
                               SIM_MAX_COMMANDS_PER_TICK, &count, &hash) == REPLAY_STATUS_OK);
    }
    CHECK(replay_require_end(&trailing) == REPLAY_STATUS_TRAILING_BYTES);

    uint8_t too_small[REPLAY_HEADER_ENCODED_SIZE - 1] = {};
    ByteWriter writer;
    byte_writer_init(&writer, too_small, sizeof(too_small));
    ReplayHeader valid = replay_header_make(1, 2, 0);
    CHECK(replay_write_header(&writer, &valid) == REPLAY_STATUS_OVERFLOW);
    CHECK(!byte_writer_ok(&writer));
    CHECK(byte_writer_size(&writer) == 0);
}
