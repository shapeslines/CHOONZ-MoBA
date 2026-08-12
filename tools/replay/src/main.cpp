#include "core/mem.h"
#include "platform/platform.h"
#include "serialize/byte_io.h"
#include "sim/replay.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const uint64_t DEFAULT_TICKS = 10000;
static const uint64_t DEFAULT_SEED = 1;
static const uint32_t DEFAULT_PLAYERS = 2;
static const size_t MAX_REPLAY_FILE_BYTES = static_cast<size_t>(256) * 1024 * 1024;
static const uint32_t PLACEHOLDER_COMMANDS_PER_TICK = 2;

enum CliExit {
    CLI_SUCCESS = 0,
    CLI_USAGE_OR_IO = 1,
    CLI_INVALID_REPLAY = 2,
    CLI_DIVERGENCE = 3,
};

static void print_usage() {
    std::printf(
        "Usage:\n"
        "  moba_replay record --out <file> [--ticks 10000] [--seed 1] [--players 2]\n"
        "  moba_replay inspect <file>\n"
        "  moba_replay verify <file>\n");
}

static bool parse_u64(const char* text, uint64_t* out) {
    if (!text || !out || text[0] == '\0' || text[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    unsigned long long value = std::strtoull(text, &end, 10);
    if (errno == ERANGE || !end || *end != '\0') return false;
    *out = static_cast<uint64_t>(value);
    return true;
}

static uint64_t mix64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

static uint32_t generate_placeholder_commands(uint64_t seed, uint64_t tick,
                                              uint32_t player_count,
                                              SimCommand* out_commands) {
    uint32_t count = 0;
    uint64_t bits = mix64(seed ^ (tick + 0x9e3779b97f4a7c15ULL));
    if ((tick % 30) == 0) {
        SimCommand command{};
        command.kind = SIM_COMMAND_SET_VELOCITY;
        command.player_id = static_cast<uint8_t>((bits >> 8) % player_count);
        command.unit_index = static_cast<uint16_t>(bits % SIM_MAX_UNITS);
        int32_t x = static_cast<int32_t>((bits >> 16) % 7) - 3;
        int32_t y = static_cast<int32_t>((bits >> 24) % 7) - 3;
        command.value_x = mm::fix_from_int(x);
        command.value_y = mm::fix_from_int(y);
        out_commands[count++] = command;
    }
    if ((tick % 17) == 0) {
        SimCommand command{};
        command.kind = SIM_COMMAND_DAMAGE;
        command.player_id = static_cast<uint8_t>((bits >> 32) % player_count);
        command.unit_index = static_cast<uint16_t>((bits >> 40) % SIM_MAX_UNITS);
        command.amount = static_cast<int32_t>((bits >> 48) % 9) + 1;
        out_commands[count++] = command;
    }
    return count;
}

static bool record_capacity(uint64_t tick_count, size_t* out_capacity) {
    if (!out_capacity) return false;
    const size_t tick_bytes = REPLAY_TICK_BASE_ENCODED_SIZE +
                              PLACEHOLDER_COMMANDS_PER_TICK * REPLAY_COMMAND_ENCODED_SIZE;
    if (tick_count > static_cast<uint64_t>((SIZE_MAX - REPLAY_HEADER_ENCODED_SIZE) / tick_bytes))
        return false;
    *out_capacity = REPLAY_HEADER_ENCODED_SIZE + static_cast<size_t>(tick_count) * tick_bytes;
    return true;
}

static int command_record(int argc, char** argv) {
    const char* out_path = nullptr;
    uint64_t tick_count = DEFAULT_TICKS;
    uint64_t seed = DEFAULT_SEED;
    uint64_t players_value = DEFAULT_PLAYERS;
    bool saw_out = false;
    bool saw_ticks = false;
    bool saw_seed = false;
    bool saw_players = false;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && !saw_out && i + 1 < argc) {
            out_path = argv[++i];
            saw_out = true;
        } else if (std::strcmp(argv[i], "--ticks") == 0 && !saw_ticks && i + 1 < argc &&
                   parse_u64(argv[i + 1], &tick_count)) {
            ++i;
            saw_ticks = true;
        } else if (std::strcmp(argv[i], "--seed") == 0 && !saw_seed && i + 1 < argc &&
                   parse_u64(argv[i + 1], &seed)) {
            ++i;
            saw_seed = true;
        } else if (std::strcmp(argv[i], "--players") == 0 && !saw_players &&
                   i + 1 < argc && parse_u64(argv[i + 1], &players_value)) {
            ++i;
            saw_players = true;
        } else {
            std::fprintf(stderr, "record: invalid or duplicate option '%s'\n", argv[i]);
            print_usage();
            return CLI_USAGE_OR_IO;
        }
    }

    if (!out_path || out_path[0] == '\0' || tick_count > REPLAY_MAX_TICKS ||
        players_value == 0 || players_value > SIM_MAX_PLAYERS) {
        std::fprintf(stderr, "record: --out is required; ticks must be 0..%" PRIu64
                             " and players 1..%u\n",
                     REPLAY_MAX_TICKS, SIM_MAX_PLAYERS);
        return CLI_USAGE_OR_IO;
    }

    size_t capacity = 0;
    if (!record_capacity(tick_count, &capacity) || capacity > MAX_REPLAY_FILE_BYTES) {
        std::fprintf(stderr, "record: requested replay exceeds the %zu-byte CLI limit\n",
                     MAX_REPLAY_FILE_BYTES);
        return CLI_USAGE_OR_IO;
    }

    Arena arena{};
    if (!platform_arena_reserve(&arena, capacity > 0 ? capacity : 1)) {
        std::fprintf(stderr, "record: could not reserve %zu bytes\n", capacity);
        return CLI_USAGE_OR_IO;
    }
    uint8_t* bytes = static_cast<uint8_t*>(arena_push(&arena, capacity, 1));
    ByteWriter writer;
    byte_writer_init(&writer, bytes, capacity);
    uint32_t player_count = static_cast<uint32_t>(players_value);
    ReplayHeader header = replay_header_make(seed, player_count, tick_count);
    ReplayStatus status = replay_write_header(&writer, &header);

    SimWorld world;
    sim_init(&world, seed);
    for (uint64_t tick = 0; tick < tick_count && status == REPLAY_STATUS_OK; ++tick) {
        SimCommand commands[PLACEHOLDER_COMMANDS_PER_TICK]{};
        uint32_t command_count = generate_placeholder_commands(seed, tick, player_count, commands);
        SimCommandBuffer command_buffer{commands, command_count};
        if (!sim_tick(&world, &command_buffer)) {
            std::fprintf(stderr, "record: internal command validation failed at tick %" PRIu64 "\n",
                         tick);
            platform_arena_release(&arena);
            return CLI_USAGE_OR_IO;
        }
        status = replay_write_tick(&writer, player_count, &command_buffer, sim_hash_state(&world));
    }

    if (status != REPLAY_STATUS_OK) {
        std::fprintf(stderr, "record: %s\n", replay_status_string(status));
        platform_arena_release(&arena);
        return CLI_USAGE_OR_IO;
    }
    size_t size = byte_writer_size(&writer);
    if (!platform_file_write(out_path, bytes, size)) {
        std::fprintf(stderr, "record: could not atomically write '%s'\n", out_path);
        platform_arena_release(&arena);
        return CLI_USAGE_OR_IO;
    }

    std::printf("recorded ticks=%" PRIu64 " players=%u bytes=%zu final_hash=0x%016" PRIx64
                " file=%s\n",
                tick_count, player_count, size, sim_hash_state(&world), out_path);
    platform_arena_release(&arena);
    return CLI_SUCCESS;
}

typedef struct LoadedReplay {
    Arena arena;
    PlatformFile file;
} LoadedReplay;

// The checked adapter prevents a file-size race from turning the arena's deliberate
// hard OOM policy into a process abort while reading untrusted replay bytes.
static void* bounded_arena_alloc(void* state, void* ptr, size_t old_size,
                                 size_t new_size, size_t align) {
    (void)ptr;
    (void)old_size;
    Arena* arena = static_cast<Arena*>(state);
    if (new_size == 0) return nullptr;
    if (align == 0) align = MEM_DEFAULT_ALIGN;
    if (!arena || align == 0 || (align & (align - 1)) != 0) return nullptr;
    uintptr_t current = reinterpret_cast<uintptr_t>(arena->base) + arena->offset;
    size_t padding = static_cast<size_t>((0 - current) & (align - 1));
    if (arena->offset > arena->reserved || padding > arena->reserved - arena->offset)
        return nullptr;
    size_t available = arena->reserved - arena->offset - padding;
    if (new_size > available) return nullptr;
    return arena_push(arena, new_size, align);
}

static int load_replay(const char* path, LoadedReplay* loaded) {
    if (!path || !loaded) return CLI_USAGE_OR_IO;
    *loaded = {};
    size_t stat_size = 0;
    if (!platform_file_size(path, &stat_size)) {
        std::fprintf(stderr, "replay: cannot stat '%s'\n", path);
        return CLI_USAGE_OR_IO;
    }
    if (stat_size > MAX_REPLAY_FILE_BYTES) {
        std::fprintf(stderr, "replay: file exceeds the %zu-byte CLI limit\n",
                     MAX_REPLAY_FILE_BYTES);
        return CLI_INVALID_REPLAY;
    }
    size_t reserve_size = stat_size > 0 ? stat_size : 1;
    if (reserve_size <= SIZE_MAX - MEM_DEFAULT_ALIGN) reserve_size += MEM_DEFAULT_ALIGN;
    if (!platform_arena_reserve(&loaded->arena, reserve_size)) {
        std::fprintf(stderr, "replay: could not reserve %zu bytes\n", reserve_size);
        return CLI_USAGE_OR_IO;
    }
    Allocator allocator{bounded_arena_alloc, &loaded->arena, ALLOC_ARENA};
    if (!platform_file_read(path, allocator, &loaded->file)) {
        std::fprintf(stderr, "replay: cannot read '%s'\n", path);
        platform_arena_release(&loaded->arena);
        return CLI_USAGE_OR_IO;
    }
    if (loaded->file.size > MAX_REPLAY_FILE_BYTES) {
        std::fprintf(stderr, "replay: file grew beyond the %zu-byte CLI limit\n",
                     MAX_REPLAY_FILE_BYTES);
        platform_arena_release(&loaded->arena);
        return CLI_INVALID_REPLAY;
    }
    return CLI_SUCCESS;
}

static ReplayStatus read_header(ByteReader* reader, const LoadedReplay& loaded,
                                ReplayHeader* header) {
    byte_reader_init(reader, loaded.file.data, loaded.file.size);
    return replay_read_header(reader, header);
}

static int command_inspect(const char* path) {
    LoadedReplay loaded{};
    int load_result = load_replay(path, &loaded);
    if (load_result != CLI_SUCCESS) return load_result;

    ByteReader reader;
    ReplayHeader header{};
    ReplayStatus status = read_header(&reader, loaded, &header);
    uint64_t total_commands = 0;
    SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
    for (uint64_t tick = 0; tick < header.tick_count && status == REPLAY_STATUS_OK; ++tick) {
        uint32_t command_count = 0;
        uint64_t expected_hash = 0;
        status = replay_read_tick(&reader, header.player_count, commands,
                                  SIM_MAX_COMMANDS_PER_TICK, &command_count, &expected_hash);
        total_commands += command_count;
    }
    if (status == REPLAY_STATUS_OK) status = replay_require_end(&reader);
    if (status != REPLAY_STATUS_OK) {
        std::fprintf(stderr, "inspect: invalid replay: %s\n", replay_status_string(status));
        platform_arena_release(&loaded.arena);
        return CLI_INVALID_REPLAY;
    }

    std::printf("magic=MOBARPLY\nformat_version=%u\nlogic_hash=0x%016" PRIx64
                "\nseed=%" PRIu64 "\ntick_rate=%u\nplayers=%u\nticks=%" PRIu64
                "\ncommands=%" PRIu64 "\nbytes=%zu\n",
                header.format_version, header.sim_logic_hash, header.seed, header.tick_rate,
                header.player_count, header.tick_count, total_commands, loaded.file.size);
    platform_arena_release(&loaded.arena);
    return CLI_SUCCESS;
}

static int command_verify(const char* path) {
    LoadedReplay loaded{};
    int load_result = load_replay(path, &loaded);
    if (load_result != CLI_SUCCESS) return load_result;

    ByteReader reader;
    ReplayHeader header{};
    ReplayStatus status = read_header(&reader, loaded, &header);
    SimWorld world;
    sim_init(&world, header.seed);
    uint64_t final_hash = sim_hash_state(&world);
    SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};

    for (uint64_t tick = 0; tick < header.tick_count && status == REPLAY_STATUS_OK; ++tick) {
        uint32_t command_count = 0;
        uint64_t expected_hash = 0;
        status = replay_read_tick(&reader, header.player_count, commands,
                                  SIM_MAX_COMMANDS_PER_TICK, &command_count, &expected_hash);
        if (status != REPLAY_STATUS_OK) break;
        SimCommandBuffer command_buffer{commands, command_count};
        if (!sim_tick(&world, &command_buffer)) {
            status = REPLAY_STATUS_BAD_COMMAND;
            break;
        }
        final_hash = sim_hash_state(&world);
        if (final_hash != expected_hash) {
            std::fprintf(stderr,
                         "verify: divergence at tick %" PRIu64
                         ": expected=0x%016" PRIx64 " actual=0x%016" PRIx64 "\n",
                         tick, expected_hash, final_hash);
            platform_arena_release(&loaded.arena);
            return CLI_DIVERGENCE;
        }
    }
    if (status == REPLAY_STATUS_OK) status = replay_require_end(&reader);
    if (status != REPLAY_STATUS_OK) {
        std::fprintf(stderr, "verify: invalid replay: %s\n", replay_status_string(status));
        platform_arena_release(&loaded.arena);
        return CLI_INVALID_REPLAY;
    }

    std::printf("verified ticks=%" PRIu64 " final_hash=0x%016" PRIx64 " file=%s\n",
                header.tick_count, final_hash, path);
    platform_arena_release(&loaded.arena);
    return CLI_SUCCESS;
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
                      std::strcmp(argv[1], "-h") == 0)) {
        print_usage();
        return CLI_SUCCESS;
    }
    if (argc >= 2 && std::strcmp(argv[1], "record") == 0) return command_record(argc, argv);
    if (argc == 3 && std::strcmp(argv[1], "inspect") == 0) return command_inspect(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "verify") == 0) return command_verify(argv[2]);
    print_usage();
    return CLI_USAGE_OR_IO;
}

