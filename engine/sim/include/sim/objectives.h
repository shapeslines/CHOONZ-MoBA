#pragma once

#include <stdint.h>

#include "sim/sim.h"

// M5.3 lane objectives (m5-lane-objectives). This file owns every piece of state the
// match layer introduces that no other file may write: the objective rows, the gold
// and XP ledgers, and the match-over verdict. check_sim_pipeline_owner.cmake enforces
// that ownership at source level, exactly the way combat.cpp owns health.current.
//
// Nothing here draws RNG (ADR-0013: this slice draws zero), nothing here writes
// health (damage routes through resolve_effect like every other effect), and every
// walk is component_pool_ordered_view - ascending entity index, never dense order.

// The match-state value a fresh world starts from: not over, no winner, no end tick.
// It lives here because objectives.cpp is the sole writer of those three fields.
SimMatchState sim_match_state_initial(void);

// Creates one objective entity: Transform at the cell centre, Health at the def's
// max_health, a Team row for its owner, and an Objective row in the ALIVE state. A
// tower never moves, so it gets no Velocity row. Failure leaves the world unchanged.
bool sim_spawn_objective(SimWorld* world, uint16_t def_index, uint32_t cell,
                         EntityId* out_entity);

// Instantiates the whole installed match: for each team in ascending team id, its
// tower then its core, at the cells the SimTeamDef names. A world with no match
// configured succeeds and creates nothing.
bool sim_spawn_match_objectives(SimWorld* world);

// Creates one lane minion at a cell: Transform, zero Velocity, Health, Team, and a
// Minion row in the PUSH state at the given waypoint index. Failure changes nothing.
bool sim_spawn_minion(SimWorld* world, uint8_t team, uint8_t lane_id, uint8_t waypoint_index,
                      uint32_t cell, EntityId* out_entity);

// True on a tick where the lane clock fires for the installed match's lane.
bool sim_wave_tick_due(const SimWorld* world);
