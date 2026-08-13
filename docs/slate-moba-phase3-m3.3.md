---
type: record
title: "SLATE - Phase 3 M3.3 fixed tick and presentation boundary"
kind: slate
status: acceptance
project: moba
axis: "simulation presentation boundary"
queued: 2026-08-12
updated: 2026-08-13
worktree: "build/m33-acceptance"
branch: "codex/m3.3-acceptance-repair"
tags: [slate, simulation, platform, presentation, determinism]
related:
  - "docs/ROADMAP.md M3.3"
  - "docs/slate-moba-phase3-m3.2.md"
  - "PR #17 / M3.2 systems and schedule"
  - "PR #28 / M3.3 acceptance repair"
---

# Slate - Phase 3 M3.3 fixed tick and presentation boundary

**Status:** implementation-complete on corrective PR #28; independent acceptance, exact-head GitHub
checks, and squash merge remain the closure gate. PRs #21–#24 merged in order, but #23/#24 were
externally merged before acceptance corrections landed. History is preserved; #28 repairs forward.

## Goal

Drive the deterministic M3.2 schedule from one platform-owned 30 Hz accumulator and expose a slim,
one-directional fixed-state snapshot seam for smooth render interpolation without allowing
presentation state or timing to feed back into `eng_sim`.

## Entry gate

- [x] PR #17 is owner-approved, merged, and present on updated `main`.
- [x] Start M3.3 from updated `main`; acceptance repair continues without rewriting merged history.
- [x] Re-run the untouched M3.2 Debug/Release stream and record `0x637628abff59c823`.

## Fence

| May touch | Must not |
|-----------|----------|
| platform-owned accumulator and frame-driving glue | M3.4 toolchain/flag consolidation |
| fixed-only `RenderSnapshot` extraction | gameplay abilities, AI, fog, pathfinding, or networking |
| double-buffered previous/current snapshots | renderer reading `SimWorld` or simulation reading presentation |
| game-owned interpolation and DrawItem construction | assets, audio, UI, packaging, or event-timing changes |
| **new `game/` target (G37 amendment, 2026-08-12): `eng_game` owns snapshot extraction, interpolation, fixed→float, and DrawItem building; platform owns fixed-step cadence** | engine modules gaining presentation deps |

## Invariants

- The executable outer loop owns the OS pump and uses platform-owned cadence state/policy for
  wall-clock accumulation, the 0.25-second clamp, owed ticks, consumption, and alpha.
- `sim_tick` remains wall-clock-free and is called zero or more whole times per outer frame.
- `RenderSnapshot` stores only fixed-point/interpolatable presentation fields and stable identities.
- Snapshot extraction runs after a completed tick and cannot mutate `SimWorld`.
- Game/present glue is the single fixed→float and interpolation owner; the renderer receives only
  `DrawItem[]` plus `FrameView` and never includes or links against simulation internals.
- Render rate, minimize/restore, skipped rendering, and interpolation alpha never affect sim hashes.
- Stale-target and backpressure policy is ADR-0014 as accepted (atomic reject); this slate changes no
  command-validation behavior.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Land and rebaseline | branch starts at merged PR #17 and the untouched M3.2 oracle is recorded | complete — oracle `0x637628abff59c823` retained |
| S1 | Snapshot storage | arena-backed fixed-only prev/curr snapshots size and initialize transactionally | complete — under-budget state/arena atomicity covered |
| S2 | Snapshot extraction | ascending entity extraction produces stable IDs and exact fixed fields without mutating world | complete — const view, 64 stable slots, actual live count, hash neutrality covered |
| S3 | Platform accumulator | variable frame deltas call exactly the expected whole 30 Hz ticks under the catch-up clamp | complete — window-free helper drives sandbox; consume follows successful tick/capture |
| S4 | Present glue | interpolation and the single fixed→float conversion build renderer inputs without a sim dependency in render | complete — previous→current and EntityId-change cases covered |
| S5 | Runtime integration | normal, high, low, and minimized render rates preserve one sim hash stream and smooth snapshots | complete — grouping/minimized tests and 90-frame RTX 4070 Ti run green |
| S6 | Close M3.3 | full matrix, tests, boundary gates, hardware/fresh-walk evidence, docs, acceptance, and GitHub gates are green | in progress — all local gates green; independent verdict and exact-head GitHub/merge pending |

## Acceptance evidence

- PR #21 exact head `04ae177` merged as `8defa10`; #22 exact head `d35adee` merged as
  `4d250b2`; #23 exact head `6a39c17` merged as `ab774ed`; #24 exact head `c56ade4` merged as
  `ca5ad22`. #20/#25 are superseded by #21. Wave 5/6 remote branches are retained.
- Corrective implementation checkpoints: platform cadence `b7bb363`, cadence slate record
  `8f5e641`, and final cadence/presentation separation `9524185`. Replay format v1,
  `SIM_LOGIC_HASH = 0xab96814425ba80a4`, same-tick command semantics, and authoritative simulation
  behavior are unchanged.
- `/WX` Debug, RelWithDebInfo, and Release builds pass all 27 CTest entries. Debug-ASan also passes
  27/27 after staging the compiler runtime app-locally for Windows CTest.
- Cadence tests prove 60 Hz, 30 Hz, mixed-rate, catch-up clamp, minimized rendering, retained debt on
  tick failure, unchanged hashes, and fresh/distinct commands for both ticks of a two-tick frame.
- Presentation tests prove transactional under-budget initialization, const/hash-neutral extraction,
  actual live count, fixed 64-slot mapping, alpha 0/midpoint/near-1, previous→current direction,
  destroyed slots, and no interpolation across reused generations.
- `sim_boundary` and `present_boundary` confirm the four dependency seams and reject clocks,
  presentation cadence, mutation, heap allocation, renderer→sim, and platform→game/sim coupling.
- A fresh clone at `9524185` completed the README configure/build/test path and rendered 90
  validation-clean frames. The RTX 4070 Ti screenshot is 1280×720 (2,764,854 bytes) with 64 objects,
  one batch, one scene draw, three total draws, and 12 allocations.
- The 10,000-tick oracle remains `0x637628abff59c823`; controlled divergence remains exactly
  `tick=4321 field=position_x entity=7`.

## Exit gate

M3.3 closes only when one platform-owned accumulator drives the unchanged deterministic schedule,
snapshot extraction is fixed-only and one-way, presentation owns the sole fixed→float conversion,
the renderer cannot see `SimWorld`, and render timing variations leave the M3.2 oracle unchanged.
M3.4 remains the separate [`structural determinism slate`](slate-moba-phase3-m3.4.md); no M3.4
implementation enters this branch.
