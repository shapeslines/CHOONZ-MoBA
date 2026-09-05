#pragma once

#include "sim/sim.h"

// M3.2 systems are plain free functions. sim_tick is the sole scheduler; these
// functions neither self-register nor discover dependencies at runtime.
bool sys_apply_commands(SimWorld* world, const SimCommandBuffer* commands);
bool sys_movement(SimWorld* world);
// M5.2: proto-design section 4 steps 5-8. Each is a no-op when its pool capacity
// is zero, so a world without heroes ticks exactly as it did before M5.2.
bool sys_hero_actions(SimWorld* world);
bool sys_projectiles(SimWorld* world);
bool sys_status(SimWorld* world);
// M5.3: proto-design section 4 steps 1, 4, and 7, implemented in objectives.cpp.
// Each returns true immediately when its feature is absent (match.team_count == 0 or
// the relevant capacity is 0), so a world without a match ticks exactly as it did
// before M5.3. None of them draws RNG.
bool sys_waves(SimWorld* world);
bool sys_minion_ai(SimWorld* world);
bool sys_combat_resolve(SimWorld* world);
bool sys_effects_resolve(SimWorld* world);
bool sys_cooldown_tick(SimWorld* world);
void sys_rng_advance(SimWorld* world);
bool sys_flush_destroy(SimWorld* world);
