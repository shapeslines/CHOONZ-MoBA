#pragma once

#include "core/sim_config.h"
#include "math/fix.h"

// The deterministic fixed-point derivative belongs at the first layer that sees
// both authorities: the shared tick rate and Q16.16's FIX_ONE.
#define SIM_DT_FIXED (FIX_ONE / SIM_HZ)
