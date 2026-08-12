---
type: record
title: "SLATE - Phase 3 M3.1 entity model and sparse-set SoA"
kind: slate
status: active
project: moba
axis: "simulation ECS storage"
queued: 2026-08-12
opened: 2026-08-12
updated: 2026-08-12
worktree: "repo root"
branch: "moba/slate-phase3-ecs"
tags: [slate, simulation, ecs, determinism]
related:
  - "docs/ROADMAP.md M3.1"
  - "docs/DECISIONS/0003-handle-abi.md"
  - "docs/slate-moba-phase3-m3.0.md"
  - "PR #14 / M3.0 determinism harness"
---

# Slate - Phase 3 M3.1 entity model and sparse-set SoA

**Status:** active on merged `main` baseline `9c14fdf`; draft PR #15.

## Goal

Replace the M3.0 placeholder storage with a real arena-backed entity manager and sparse-set SoA
component pools while preserving the established replay/hash stream as the regression oracle.

## Entry gate

- [x] PR #14 is squash-merged as `9c14fdf`; its Debug/Release CI and CodeQL gates were green.
- [x] Create `moba/slate-phase3-ecs` from that updated `main`.
- [x] Capture the M3.0 Debug/Release determinism baseline before changing `SimWorld` layout.

## Fence

| May touch | Must not |
|-----------|----------|
| `engine/sim/**` entity and component storage | M3.2 system scheduler/event queues |
| focused core allocator/handle tests if required | M3.3 platform accumulator or presentation snapshots |
| replay/hash migration required by authoritative ECS fields | renderer, assets, networking, or gameplay |
| M3.1 tests and documentation | generic ECS framework, archetypes, jobs, reflection, query DSL |

## Invariants

- `EntityId` remains the ADR-0003 18-bit index + 14-bit generation wrapper; generation zero and
  `HANDLE_NULL` are invalid. Generation storage must preserve all 14 bits.
- Entity and pool capacities are fixed up front and arena-backed; no heap allocation enters sim.
- Sparse maps entity index to dense slot + 1; dense slots map back to the owning `EntityId`.
- Removal may swap the last dense element but must repair both directions atomically.
- Destroy is requested during work and committed only at the end-of-tick boundary.
- Every new authoritative field is added to canonical hash/diff order and therefore requires a
  deliberate `SIM_LOGIC_HASH` bump; replay format version stays `1` unless container layout changes.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Rebaseline and specify storage contracts | M3.0 10,000-tick Debug/Release oracle captured; capacities, failure behavior, generation wrap policy, and canonical hash migration are written down | done 2026-08-12 |
| S1 | Add arena-backed `EntityManager` | create/alive/destroy/recycle tests pass; stale IDs fail; exhaustion is explicit; generation zero is never issued | done 2026-08-12 |
| S2 | Add generic sparse/dense membership core | add/remove/has/dense lookup stay O(1); swap-remove repairs sparse and back-reference maps under adversarial sequences | done 2026-08-12 |
| S3 | Add Transform, Velocity, and Health SoA pools | all component data is arena-backed and indexed through validated `EntityId`; capacity and duplicate/missing operations are covered | done 2026-08-12 |
| S4 | Integrate deferred destruction into `SimWorld` | queued destroys commit once at the tick boundary, remove all components, invalidate stale IDs, and cannot partially mutate on failure | done 2026-08-12 |
| S5 | Migrate canonical hash/diff and replay oracle | all authoritative entity/pool fields have an explicit LE order; logic hash is deliberately bumped; run-twice and exact-divergence tests stay green | done 2026-08-12 |
| S6 | Close M3.1 | full `/WX` Debug/Release CTest and boundary gates pass; docs are current; M3.2 gets a separate slate | queued |

## Exit gate

M3.1 closes only when entity lifecycle, sparse-set repair, SoA storage, deferred destruction, stale
generation rejection, canonical hash migration, and the full M3.0 replay oracle are green in Debug
and Release. M3.2 scheduling remains a separate decision and implementation slate.

## Slice evidence

- **S0:** PR #14 merged without stranded commits (`headRefOid` matched the shipped head
  `2fb8f0d`; squash `9c14fdf`). The untouched M3.0 `sim_determinism` test passed from the `ci`
  preset in Debug and Release, retaining the 10,000-tick final hash `0xb85d4b632571948c` and exact
  `tick=4321 field=position_x unit=7` diagnostic. M3.1 is locked to runtime capacity configuration
  (default 16,384), replay-v1 stable unit slots, a temporary cooldown field in `HealthPool`, and
  logic hash `0x7902599e173f87a6`.
- **S1:** Added the dedicated arena-backed `EntityManager` with 16-bit generations, explicit
  liveness, ascending fresh allocation, LIFO recycling, and atomic exhaustion/stale failure.
  `sim_entity` and the affected `sim` suite pass under the `ci` preset in Debug and Release;
  Release explicitly proves deterministic generation wrap to one. The Debug sim-boundary scan
  remains green, and initialization tests prove invalid and under-budget attempts preserve both
  the output object and arena offset.
- **S2:** Added the arena-backed `ComponentPool` membership core with sparse index to dense-slot
  plus one and exact-`EntityId` dense back-references. Focused tests cover initialization budgets,
  duplicate/stale/full/missing atomic failures, first/middle/last swap-removal, and 1,024 churn
  operations with both directions checked after every mutation. `sim_component_pool`,
  `sim_entity`, `sim`, and `sim_boundary` pass under the `ci` preset in Debug and Release.
- **S3 checkpoint A:** Transform, Velocity, and Health typed SoA pools now allocate every lane from
  the arena, expose validated mutable pointer views, and repair all parallel values after sparse-set
  swap-removal. Focused typed-pool plus affected sim suites and the boundary scan pass under the
  `ci` preset in Debug and Release.
- **S3 checkpoint B:** `SimWorldConfig`, transactional memory sizing/initialization, the entity
  manager, three pools, stable 64-slot mappings, and the pre-budgeted destroy queue now form the
  world. The default 16,384-capacity configuration initializes the original 64-unit grid and exact
  values; compact configs permit 0-64 initial units. Commands validate the entire slot/component
  schedule before mutation, apply in recorded order, and integrate in ascending unit-slot order.
  The deliberate M3.1 logic hash is now `0x7902599e173f87a6`; replay v1 and 16-byte commands are
  unchanged. Debug and Release affected suites, CLI round-trip/error fixtures, boundary scan, and
  the 10,000-tick oracle all pass. Canonical ECS metadata hashing remains fenced to S5.
- **S4:** Added `sim_destroy_deferred` and end-of-tick commit in literal request order. Focused
  tests prove null/stale/duplicate/full requests are atomic, queued entities remain command-usable
  for the current tick, Health/Velocity/Transform and unit mappings disappear exactly at the
  boundary, stale IDs never resolve after reuse, and release order produces deterministic LIFO
  recycling. A rejected tick preserves its pending queue byte-for-byte, while componentless
  unmapped live entities also destroy cleanly. Debug/Release affected suites, replay fixtures,
  10,000-tick oracle, and boundary scan remain green under the `ci` preset.
- **S5:** Replaced the transitional unit projection with canonical FNV-1a/64 encoding over explicit
  little-endian config, RNG, entity-manager scalars, generations/liveness, free-stack order, all
  64 mappings, pending order, and typed-pool membership/values in ascending entity-index order.
  `sim_diff_state` mirrors the same order and index domains. Tests prove pointer, padding-by-
  construction, unused-capacity, allocator, and repaired sparse/dense order exclusion; lifecycle,
  mapping, queue, membership, facing, health maximum, and every other authoritative value affect
  the stream. Replay v1 rejects the exact M3.0 logic hash. Debug and Release independently produce
  the pinned 10,000-tick M3.1 golden `0x981212877a575730`, with first controlled divergence exactly
  `tick=4321 field=position_x entity=7`; CLI record/inspect/verify and error classes remain green.
