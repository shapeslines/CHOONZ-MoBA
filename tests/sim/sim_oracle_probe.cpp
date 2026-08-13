#include "sim/oracle.h"

#include <cstdio>
#include <cstring>

alignas(16) static uint8_t g_world_storage[SIM_ORACLE_WORLD_STORAGE_SIZE];

int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "--sim-self-check") != 0) {
        std::printf("usage: sim_oracle_probe --sim-self-check\n");
        return 1;
    }

    SimOracleResult result{};
    if (!sim_oracle_run(g_world_storage, sizeof(g_world_storage), &result)) {
        std::printf("sim oracle failed\n");
        return 2;
    }

    std::printf(
        "sim_oracle ticks=%llu commands=%llu final=0x%016llx stream=0x%016llx logic=0x%016llx\n",
        static_cast<unsigned long long>(result.tick_count),
        static_cast<unsigned long long>(result.command_count),
        static_cast<unsigned long long>(result.final_state_hash),
        static_cast<unsigned long long>(result.hash_stream_digest),
        static_cast<unsigned long long>(result.logic_hash));
    return 0;
}
