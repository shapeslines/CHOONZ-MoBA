---
type: record
title: "SLATE - Phase 3 M3.1 entity model and sparse-set SoA"
kind: slate
status: queued
project: moba
axis: "simulation ECS storage"
queued: 2026-08-12
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

**Status:** queued; do not open until PR #14 lands on `main`.

## Goal

Replace the M3.0 placeholder storage with a real arena-backed entity manager and sparse-set SoA
component pools while preserving the established replay/hash stream as the regression oracle.

## Entry gate

- [ ] PR #14 is merged and GitHub CI/CodeQL are green on the merge commit.
- [ ] Create `moba/slate-phase3-ecs` from that updated `main`.
- [ ] Capture the M3.0 Debug/Release determinism baseline before changing `SimWorld` layout.

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
| S0 | Rebaseline and specify storage contracts | M3.0 10,000-tick Debug/Release oracle captured; capacities, failure behavior, generation wrap policy, and canonical hash migration are written down | queued |
| S1 | Add arena-backed `EntityManager` | create/alive/destroy/recycle tests pass; stale IDs fail; exhaustion is explicit; generation zero is never issued | queued |
| S2 | Add generic sparse/dense membership core | add/remove/has/dense lookup stay O(1); swap-remove repairs sparse and back-reference maps under adversarial sequences | queued |
| S3 | Add Transform, Velocity, and Health SoA pools | all component data is arena-backed and indexed through validated `EntityId`; capacity and duplicate/missing operations are covered | queued |
| S4 | Integrate deferred destruction into `SimWorld` | queued destroys commit once at the tick boundary, remove all components, invalidate stale IDs, and cannot partially mutate on failure | queued |
| S5 | Migrate canonical hash/diff and replay oracle | all authoritative entity/pool fields have an explicit LE order; logic hash is deliberately bumped; run-twice and exact-divergence tests stay green | queued |
| S6 | Close M3.1 | full `/WX` Debug/Release CTest and boundary gates pass; docs are current; M3.2 gets a separate slate | queued |

## Exit gate

M3.1 closes only when entity lifecycle, sparse-set repair, SoA storage, deferred destruction, stale
generation rejection, canonical hash migration, and the full M3.0 replay oracle are green in Debug
and Release. M3.2 scheduling remains a separate decision and implementation slate.
