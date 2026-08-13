#pragma once

// The one-way SIM/PRESENTATION boundary (M3.3). eng_game owns fixed-only snapshot
// extraction, previous/current storage, interpolation, and the sole fixed->float
// conversion. Platform owns wall-clock cadence; the renderer never sees SimWorld.

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"
#include "render/renderer_types.h"
#include "sim/sim.h"

typedef struct RenderSnapshotUnit {
    EntityId entity; // HANDLE_NULL marks an empty unit slot
    mm::fix  x;
    mm::fix  y;
    mm::fix  facing;
} RenderSnapshotUnit;

typedef struct RenderSnapshot {
    uint64_t tick;
    uint32_t live_count; // actual live mapped units, not the last occupied slot + 1
    RenderSnapshotUnit* units; // always SIM_MAX_UNITS entries, indexed by replay unit slot
} RenderSnapshot;

typedef struct PresentState {
    RenderSnapshot previous;
    RenderSnapshot current;
} PresentState;

// Upper bound independent of arena base alignment. Storage is two fixed 64-slot
// arrays; no heap allocation or allocator state is retained.
size_t present_memory_required(void);

// Transactional: failure leaves both state and arena offset unchanged. Both snapshots
// start at initial_world, so the pre-first-tick frame never interpolates from zero.
bool present_init(PresentState* state, Arena* arena, const SimWorld* initial_world);

// Writes all 64 stable unit slots and the actual live count. Failure is explicit and
// leaves out unchanged. The const Transform view keeps extraction read-only by type.
bool snapshot_extract(const SimWorld* world, RenderSnapshot* out);

// Captures a successfully completed tick. The old current snapshot becomes previous;
// current is extracted into the other arena-backed buffer.
bool present_capture(PresentState* state, const SimWorld* world);

// Builds renderer inputs at platform-computed alpha. Same-identity slots interpolate
// previous -> current; a changed EntityId uses current directly to avoid generation
// blending. Returns items written (<= out_capacity).
uint32_t present_build_draw_items(const PresentState* state, double alpha,
                                  MeshHandle mesh, MaterialHandle material,
                                  DrawItem* out_items, uint32_t out_capacity);
