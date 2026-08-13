#pragma once

// Window-independent fixed-step cadence owned by the platform layer (ADR-0001/0005).
// The caller adds wall-clock frame deltas, runs each owed simulation tick, and consumes
// the owed tick only after that work succeeds. This module knows cadence, not SimWorld.

typedef struct PlatformFixedStep {
    double accumulator_seconds;
} PlatformFixedStep;

void   platform_fixed_step_init(PlatformFixedStep* step);
void   platform_fixed_step_add_frame_delta(PlatformFixedStep* step, double frame_delta_seconds);
bool   platform_fixed_step_tick_owed(const PlatformFixedStep* step);
bool   platform_fixed_step_consume_tick(PlatformFixedStep* step);
double platform_fixed_step_alpha(const PlatformFixedStep* step);
