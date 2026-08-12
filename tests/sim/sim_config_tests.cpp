#include "test.h"
#include "core/sim_config.h"
#include "sim/sim_config.h"

static_assert(SIM_HZ == 30, "ADR-0001 fixes the simulation rate at 30 Hz");
static_assert(SIM_DT_FIXED == 2184, "Q16.16 fixed dt must truncate deterministically");
static_assert(SIM_DT_FIXED == FIX_ONE / SIM_HZ, "fixed dt must derive from both authorities");
static_assert(SIM_DT_SECONDS > 0.033 && SIM_DT_SECONDS < 0.034, "wall dt must derive from SIM_HZ");
static_assert(SIM_MAX_CATCHUP_S == 0.25, "accumulator clamp is part of the shared contract");

TEST(sim, shared_config_contract) {
    volatile int hz = SIM_HZ;
    volatile int dt_fixed = SIM_DT_FIXED;
    CHECK(hz == 30);
    CHECK(dt_fixed == FIX_ONE / hz);
    CHECK(dt_fixed * hz <= FIX_ONE);
    CHECK(FIX_ONE - dt_fixed * hz < hz);
}
