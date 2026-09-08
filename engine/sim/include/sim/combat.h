#pragma once

#include <stdint.h>

#include "sim/sim.h"

// M5.2 unified effect pipeline (proto-design section 3.3, last paragraph): every
// data-defined effect enters ONE function. The stages are fixed and ordered:
//
//   validate (both handles alive, required components present, effect canonical)
//   -> base magnitude (from the def)
//   -> modifiers (data only; none in v1)
//   -> mitigation (armor placeholder, always 0 in v1)
//   -> append event
//
// resolve_effect only APPENDS. State is committed after publish by
// sys_combat_resolve (damage) and sys_effects_resolve (heal, status, death), so
// append order is commit order. No other function in engine/sim/src may write
// health.current, hero resource, or any cooldown; sys_cooldown_tick is the sole
// decrementer. That rule is what makes an added ability a data change rather than
// one more mutation path.
//
// Range and area are squared-distance comparisons on TransformPool positions in a
// 64-bit intermediate. v1 hero combat never calls map_*: no line of sight, no cell
// occupancy, no pathing. Those are m5-lane-objectives.

bool resolve_effect(SimWorld* world, EntityId source, EntityId target,
                    const SimEffectDef* effect, mm::fix point_x, mm::fix point_y);

// The same walk with no mutation: reports how many DamageEvent and SimEvent slots
// the effect would consume. Producers pre-flight a whole action with this so an
// event-queue overflow fails the source operation before it changes anything
// (ADR-0014: reject at source, never drop).
bool resolve_effect_measure(const SimWorld* world, EntityId source, EntityId target,
                            const SimEffectDef* effect, mm::fix point_x, mm::fix point_y,
                            uint32_t* damage_events, uint32_t* sim_events);

// v1 basic attack. Section 3.3's HeroDef carries no basic-attack magnitude, so the
// sim synthesizes one placeholder effect and routes it through the same pipeline
// rather than opening a second mutation path.
SimEffectDef sim_basic_attack_effect(void);

// Squared distance between two live transforms, in a 64-bit Q32.32 intermediate.
// Returns false when either entity has no transform.
bool sim_distance_squared(const SimWorld* world, EntityId a, EntityId b, int64_t* out);
bool sim_distance_squared_point(const SimWorld* world, EntityId entity,
                                mm::fix point_x, mm::fix point_y, int64_t* out);

// range is Q16.16; the comparison widens to Q32.32 because a Q16.16 square
// overflows 32 bits.
int64_t sim_range_squared(mm::fix range);
