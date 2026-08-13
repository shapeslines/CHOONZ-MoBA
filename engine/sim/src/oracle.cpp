#include "sim/oracle.h"

#include "core/mem.h"
#include "sim/replay.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

static const uint64_t FNV1A_OFFSET_BASIS_64 = 14695981039346656037ULL;
static const uint64_t FNV1A_PRIME_64 = 1099511628211ULL;

static uint64_t hash_u64_le(uint64_t hash, uint64_t value) {
    for (uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<uint8_t>(value >> (byte * 8));
        hash *= FNV1A_PRIME_64;
    }
    return hash;
}

bool sim_oracle_run(void* world_storage, size_t world_storage_size,
                    SimOracleResult* out_result) {
    if (!world_storage || !out_result) return false;

    const SimWorldConfig config = sim_world_config_default();
    if (world_storage_size < sim_world_memory_required(config)) return false;

    Arena arena{};
    arena_init_fixed(&arena, world_storage, world_storage_size);
    SimWorld world{};
    if (!sim_init(&world, &arena, SIM_ORACLE_SEED, config)) return false;

    SimOracleResult result{};
    result.tick_count = SIM_ORACLE_TICKS;
    result.hash_stream_digest = FNV1A_OFFSET_BASIS_64;
    result.logic_hash = SIM_LOGIC_HASH;
    for (uint64_t tick = 0; tick < SIM_ORACLE_TICKS; ++tick) {
        SimCommand commands[REPLAY_PLACEHOLDER_MAX_COMMANDS]{};
        const uint32_t command_count = replay_generate_placeholder_commands(
            SIM_ORACLE_SEED, tick, SIM_ORACLE_PLAYERS, commands);
        const SimCommandBuffer command_buffer{commands, command_count};
        if (!sim_tick(&world, &command_buffer)) return false;

        result.command_count += command_count;
        result.final_state_hash = sim_hash_state(&world);
        result.hash_stream_digest =
            hash_u64_le(result.hash_stream_digest, result.final_state_hash);
    }

    *out_result = result;
    return true;
}
