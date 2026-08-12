#pragma once

// Shared simulation cadence. Core owns the rate and wall-clock accumulator policy;
// fixed-point derivatives live in eng_sim so core and math remain independent leaves
// (ADR-0001/0006).
#define SIM_HZ              30
#define SIM_DT_SECONDS      (1.0 / (double)SIM_HZ)
#define SIM_MAX_CATCHUP_S   0.25
