#include "platform/platform_fixed_step.h"

#include "core/sim_config.h"

static const double FIXED_STEP_EPSILON = SIM_DT_SECONDS * 1.0e-9;

void platform_fixed_step_init(PlatformFixedStep* step) {
    if (!step) return;
    step->accumulator_seconds = 0.0;
}

void platform_fixed_step_add_frame_delta(PlatformFixedStep* step, double frame_delta_seconds) {
    if (!step || !(frame_delta_seconds > 0.0)) return; // also rejects NaN

    const double remaining = SIM_MAX_CATCHUP_S - step->accumulator_seconds;
    if (!(remaining > 0.0) || frame_delta_seconds >= remaining) {
        step->accumulator_seconds = SIM_MAX_CATCHUP_S;
        return;
    }
    step->accumulator_seconds += frame_delta_seconds;
}

bool platform_fixed_step_tick_owed(const PlatformFixedStep* step) {
    return step && step->accumulator_seconds + FIXED_STEP_EPSILON >= SIM_DT_SECONDS;
}

bool platform_fixed_step_consume_tick(PlatformFixedStep* step) {
    if (!platform_fixed_step_tick_owed(step)) return false;
    step->accumulator_seconds -= SIM_DT_SECONDS;
    if (step->accumulator_seconds < 0.0) step->accumulator_seconds = 0.0;
    return true;
}

double platform_fixed_step_alpha(const PlatformFixedStep* step) {
    if (!step || !(step->accumulator_seconds > 0.0)) return 0.0;
    double alpha = step->accumulator_seconds / SIM_DT_SECONDS;
    if (alpha > 1.0) alpha = 1.0;
    return alpha;
}
