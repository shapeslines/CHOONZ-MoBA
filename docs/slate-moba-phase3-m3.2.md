---
type: record
title: "SLATE - Phase 3 M3.2 systems and ordered schedule"
kind: slate
status: active
project: moba
axis: "simulation systems scheduling"
queued: 2026-08-12
updated: 2026-08-12
worktree: "repo root"
branch: "moba/slate-phase3-systems"
tags: [slate, simulation, systems, determinism]
related:
  - "docs/ROADMAP.md M3.2"
  - "docs/slate-moba-phase3-m3.1.md"
  - "PR #15 / M3.1 entity model"
---

# Slate - Phase 3 M3.2 systems and ordered schedule

**Status:** active on `moba/slate-phase3-systems` from merged `main` at `7ce2b45`.

## Goal

Move the proven M3.1 storage through explicit plain-function systems with deterministic ordered
iteration and append-only typed events, while retaining the M3.1 replay command seam and hash oracle.

## Entry gate

- [x] PR #15 is owner-approved, merged, and present on updated `main`.
- [x] Create `moba/slate-phase3-systems` from that updated `main`.
- [x] Re-run the untouched M3.1 Debug/Release 10,000-tick stream and record
  `0x981212877a575730` before schedule changes.

Observed 2026-08-12: PR #15 merged the exact reviewed head
`45b317b9ea45bd7b2352f924208e471b634ba01f` as squash `7ce2b45`. The untouched
branch point builds under `/WX`; all 22 CTest entries pass in Debug and Release,
both 10,000-tick runs retain the M3.1 golden, and the controlled divergence remains
`tick=4321 field=position_x entity=7`.

## Fence

| May touch | Must not |
|-----------|----------|
| ordered per-pool iteration caches | M3.3 platform accumulator or presentation snapshots |
| typed append-only/double-buffered sim event queues | gameplay abilities, AI, fog, pathfinding, or networking |
| plain movement and damage-resolution systems | renderer, assets, platform, or Vulkan |
| explicit `sim_tick` schedule and canonical state migration | archetypes, query DSL, jobs, reflection, or generic scheduler |

## Invariants

- Order-sensitive systems consume ascending entity indices; raw dense order is allowed only for a
  documented commutative system.
- Sorted index caches rebuild only when membership changes and never become hash authority; semantic
  state remains ordered by entity index.
- Event queues are fixed-capacity, arena-backed, append-only POD records drained in append order.
  Overflow and invalid events fail without partial mutation; there are no callbacks or observers.
- M3.2 resolves command-produced damage in the same tick to preserve the M3.1 command contract.
  The queue owns explicit read/write phases so a later measured switch to next-tick delivery changes
  schedule policy rather than queue storage or event encoding.
- Systems are plain free functions in one hand-written schedule. No auto-discovery, dependency graph,
  virtual dispatch, heap allocation, or unordered iteration enters `eng_sim`.
- Replay stays version 1 with 16-byte unit-slot commands. Any deterministic behavior change receives
  a deliberately reviewed logic-hash bump; container layout alone controls format version.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Land and rebaseline | branch starts at merged PR #15 and the untouched M3.1 Debug/Release oracle is recorded | done 2026-08-12 |
| S1 | Ordered iteration cache | membership churn rebuilds a unique ascending entity-index view; dense reorder cannot affect it | done 2026-08-12 |
| S2 | Typed event queues | fixed-capacity double buffers append/drain deterministically and reject overflow atomically | done 2026-08-12 |
| S3 | Plain systems | movement and damage-resolution systems consume typed views/events with no storage-order dependency | done 2026-08-12 |
| S4 | Explicit schedule | one literal `sim_tick` order runs commands, movement, damage, cleanup, RNG, and tick-boundary destruction | done 2026-08-12 |
| S5 | Determinism migration | canonical hash/diff covers authoritative queues/system state; 10,000 ticks and exact mutation proof pass in Debug/Release | queued |
| S6 | Close M3.2 | full `/WX` matrix, all CTest, boundary scan, docs, independent acceptance, and GitHub gates are green | queued |

## Exit gate

M3.2 closes only when movement and damage events run through the explicit schedule, every
order-sensitive traversal is ascending by entity index, event overflow is atomic, and the complete
Debug/Release replay oracle and boundary gates remain green. M3.3 stays a separate slate.

## Slice evidence

- **S1:** `ComponentPool` now owns an arena-backed derived `EntityId` cache. Successful membership
  changes mark it dirty; the ordered-view API validates the complete sparse/dense mapping before an
  atomic rebuild by ascending sparse index. Focused Debug tests cover divergent dense layouts,
  swap-remove/re-add churn, stable clean-cache reads, and failure atomicity. The affected sim targets,
  M3.1 golden stream, exact tick-4321 diagnostic, and sim-boundary test remain green.
- **S2:** Added the typed 12-byte `DamageEvent` and an arena-backed two-buffer queue with explicit
  append, publish, read, and consume phases. The world configuration owns queue capacity (256 by
  default), sizing and initialization remain transactional, overflow/invalid/unread-publish failures
  are byte-for-byte mutation-free, and a focused test demonstrates that deferring only `publish`
  produces the future next-tick policy. All affected sim/replay targets and the unchanged M3.1 oracle
  remain green.
- **S3:** Extracted command production, ordered movement, append-order combat resolution, ordered
  cooldown ticking, deterministic RNG advance, and destruction cleanup into prefixed free functions.
  Focused tests prove command/event order, identical movement across divergent dense layouts, and
  whole-read-phase combat validation before damage mutation. The affected Debug targets, destruction
  suite, sim boundary, unchanged 10,000-tick golden, and exact divergence diagnostic remain green.
- **S4:** `sim_tick` now names the literal schedule directly: validate, apply commands, ordered
  movement, publish damage, combat resolve, consume, cooldown, RNG, deferred destruction, tick
  increment. Schedule tests distinguish same-tick command/movement and damage/cooldown order, prove
  one event-buffer swap and RNG advance per tick, retain end-boundary destruction, and reject event
  overflow byte-for-byte before an earlier velocity command can mutate state.
