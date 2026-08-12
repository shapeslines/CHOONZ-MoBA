---
type: record
title: "SLATE - Phase 3 M3.3 fixed tick and presentation boundary"
kind: slate
status: queued
project: moba
axis: "simulation presentation boundary"
queued: 2026-08-12
updated: 2026-08-12
worktree: "repo root"
branch: "moba/slate-phase3-presentation"
tags: [slate, simulation, platform, presentation, determinism]
related:
  - "docs/ROADMAP.md M3.3"
  - "docs/slate-moba-phase3-m3.2.md"
  - "PR #17 / M3.2 systems and schedule"
---

# Slate - Phase 3 M3.3 fixed tick and presentation boundary

**Status:** queued behind the owner-approved merge of PR #17. Do not stack on the M3.2 branch.

## Goal

Drive the deterministic M3.2 schedule from one platform-owned 30 Hz accumulator and expose a slim,
one-directional fixed-state snapshot seam for smooth render interpolation without allowing
presentation state or timing to feed back into `eng_sim`.

## Entry gate

- [ ] PR #17 is owner-approved, merged, and present on updated `main`.
- [ ] Create `moba/slate-phase3-presentation` from that updated `main`.
- [ ] Re-run the untouched M3.2 Debug/Release stream and record `0x637628abff59c823`.

## Fence

| May touch | Must not |
|-----------|----------|
| platform-owned accumulator and frame-driving glue | M3.4 toolchain/flag consolidation |
| fixed-only `RenderSnapshot` extraction | gameplay abilities, AI, fog, pathfinding, or networking |
| double-buffered previous/current snapshots | renderer reading `SimWorld` or simulation reading presentation |
| game-owned interpolation and DrawItem construction | assets, audio, UI, packaging, or event-timing changes |

## Invariants

- The platform outer loop owns wall-clock accumulation, the OS pump, and the 0.25-second clamp.
- `sim_tick` remains wall-clock-free and is called zero or more whole times per outer frame.
- `RenderSnapshot` stores only fixed-point/interpolatable presentation fields and stable identities.
- Snapshot extraction runs after a completed tick and cannot mutate `SimWorld`.
- Game/present glue is the single fixed→float and interpolation owner; the renderer receives only
  `DrawItem[]` plus `FrameView` and never includes or links against simulation internals.
- Render rate, minimize/restore, skipped rendering, and interpolation alpha never affect sim hashes.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Land and rebaseline | branch starts at merged PR #17 and the untouched M3.2 oracle is recorded | queued |
| S1 | Snapshot storage | arena-backed fixed-only prev/curr snapshots size and initialize transactionally | queued |
| S2 | Snapshot extraction | ascending entity extraction produces stable IDs and exact fixed fields without mutating world | queued |
| S3 | Platform accumulator | variable frame deltas call exactly the expected whole 30 Hz ticks under the catch-up clamp | queued |
| S4 | Present glue | interpolation and the single fixed→float conversion build renderer inputs without a sim dependency in render | queued |
| S5 | Runtime integration | normal, high, low, and minimized render rates preserve one sim hash stream and smooth snapshots | queued |
| S6 | Close M3.3 | full matrix, tests, boundary gates, owner interaction check if needed, docs, acceptance, and GitHub gates are green | queued |

## Exit gate

M3.3 closes only when one platform-owned accumulator drives the unchanged deterministic schedule,
snapshot extraction is fixed-only and one-way, presentation owns the sole fixed→float conversion,
the renderer cannot see `SimWorld`, and render timing variations leave the M3.2 oracle unchanged.
M3.4 remains a separate closure slate.
