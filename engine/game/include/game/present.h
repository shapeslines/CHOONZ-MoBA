#pragma once
// The SIM/PRESENTATION snapshot boundary (M3.3; ARCHITECTURE §2.1) — owned by the
// game/present glue (G37). The rules, in one header:
//
//  - RenderSnapshot is the ONLY thing that crosses sim -> presentation: fixed-point
//    presentation fields + stable identities, extracted by snapshot_extract(), which
//    never mutates SimWorld (const) and never feeds anything back.
//  - present_advance() owns the fixed-tick accumulator (wall-clock, clamped by
//    SIM_MAX_CATCHUP_S) and the double-buffered prev/curr snapshots. sim_tick stays
//    wall-clock-free; grouping ticks differently (render rate) never changes the
//    sim's hash stream — only WHEN sim_tick is called changes.
//  - present_build_draw_items() is the single fixed->float conversion and
//    interpolation owner (ARCHITECTURE §2.1). The renderer receives only DrawItem[]
//    plus FrameView and never sees SimWorld.
//
// This module never writes to SimWorld except through sim_tick (via present_advance).

#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"
#include "render/renderer_types.h"
#include "sim/sim.h"

typedef struct RenderSnapshotUnit {
    EntityId entity;             // stable identity; HANDLE_NULL marks a dead slot
    mm::fix  x;                  // world units, fixed-point (Q16.16)
    mm::fix  y;
    mm::fix  facing;             // radians, fixed-point
} RenderSnapshotUnit;

typedef struct RenderSnapshot {
    uint32_t tick;                          // sim tick this snapshot was extracted at
    uint32_t count;                         // entries valid (up to the last live slot + 1)
    RenderSnapshotUnit units[SIM_MAX_UNITS];  // SLOT-indexed: interpolation pairs
                                              // prev.units[i] with curr.units[i]
} RenderSnapshot;

typedef struct PresentState {
    double accumulator;                     // seconds of pending sim time (presentation wall clock)
    RenderSnapshot prev, curr;              // prev = tick-1, curr = latest tick
    double alpha;                           // [0,1) interpolation fraction for the current frame
} PresentState;

void present_init(PresentState* p);

// Extracts the current sim state into a fixed-only snapshot. Non-mutating: the world
// hash is untouched. Returns the number of valid entries written (<= SIM_MAX_UNITS).
uint32_t snapshot_extract(const SimWorld* world, RenderSnapshot* out);

// Accumulates frame_delta seconds (clamped to SIM_MAX_CATCHUP_S), runs the whole
// ticks owed (sim_tick + double-buffered snapshot extraction), and computes alpha.
// Returns the number of ticks run. Commands are passed through to sim_tick verbatim
// (atomic-reject semantics per ADR-0014: a rejected buffer applies nothing).
uint32_t present_advance(PresentState* p, double frame_delta, SimWorld* world,
                         const SimCommandBuffer* commands);

// Interpolates prev -> curr at p->alpha and builds DrawItems. THE single fixed->float
// conversion point. Returns items written (<= out_capacity).
uint32_t present_build_draw_items(const PresentState* p, const MeshHandle mesh,
                                  const MaterialHandle material, Arena* frame_arena,
                                  DrawItem* out_items, uint32_t out_capacity);
