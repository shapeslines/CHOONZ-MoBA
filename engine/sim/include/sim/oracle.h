#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint64_t SIM_ORACLE_SEED = 1;
static const uint32_t SIM_ORACLE_PLAYERS = 2;
static const uint64_t SIM_ORACLE_TICKS = 10000;
static const size_t SIM_ORACLE_WORLD_STORAGE_SIZE = 2u * 1024u * 1024u;

typedef struct SimOracleResult {
    uint64_t tick_count;
    uint64_t command_count;
    uint64_t final_state_hash;
    uint64_t hash_stream_digest;
    uint64_t logic_hash;
} SimOracleResult;

// Runs the canonical M3.2/M3.3 placeholder stream without platform, wall-clock,
// renderer, or heap input. The caller owns fixed storage so both game and test
// executables exercise the same eng_sim object code under the same contract.
bool sim_oracle_run(void* world_storage, size_t world_storage_size,
                    SimOracleResult* out_result);
