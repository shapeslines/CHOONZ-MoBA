---
type: record
title: "SLATE - Phase 3 M3.0 determinism harness"
kind: slate
status: queued
project: moba
axis: "simulation determinism"
opened: 2026-08-12
updated: 2026-08-12
closed:
worktree: "new branch from main after PR #13 lands"
branch: "moba/slate-phase3-determinism"
tags: [slate, determinism, simulation]
related:
  - "docs/ROADMAP.md M3.0"
  - "docs/DECISIONS/0001-tick-rate.md"
  - "docs/DECISIONS/0002-fixed-point-sim-math.md"
  - "docs/DECISIONS/0007-sim-rng.md"
  - "PR #13 / Phase 2 closeout"
---

# Slate - Phase 3 M3.0 determinism harness

**Status:** queued behind PR #13 landing. The Phase 2 S6 owner gate passed on 2026-08-12.

## Goal

Stand up the desync detector before the real ECS exists: a small fixed-point `SimWorld`, a
canonical state hash, a replay command stream, and a 10,000-tick run-twice proof that reports the
first divergent tick exactly.

This slate is M3.0 only. M3.1 ECS work starts in a later slate after the determinism harness is
green, so changes to entity storage are immediately measurable against an established oracle.

## Entry gate

- [x] Phase 2 S6 passes with zero Vulkan validation messages.
- [ ] PR #13 lands on `main`.
- [ ] Create `moba/slate-phase3-determinism` from the updated `main`; do not stack simulation work on
  the renderer branch.

## Fence

| May touch | Must not |
|-----------|----------|
| `engine/core/include/core/sim_config.h` | `engine/render/**` and Vulkan behavior |
| new `engine/sim/**` | M3.1 entity manager/component pools |
| focused replay persistence glue under `tools/**` | Phase 4 assets or Phase 6 networking |
| determinism tests and top-level CMake registration | allocator redesign or hosted-CI Vulkan setup |
| M3.0 documentation | gameplay content, balance, or presentation conversion |

## Architecture constraints

- `eng_sim` depends on `eng_core` and `eng_math` only. It must not link platform, renderer, OS,
  wall-clock, or floating-point code.
- `core/sim_config.h` is the single owner of `SIM_HZ`, `SIM_DT_SECONDS`, `SIM_DT_FIXED`, and
  `SIM_MAX_CATCHUP_S`, matching ADR-0001. The documented header is currently missing and is the
  first repair.
- State hashing consumes explicit fields in an explicit order. Never hash struct padding, raw
  capacity, pointers, render data, timing data, or debug state.
- Replay encoding is hand-written and little-endian. The deterministic codec stays independent of
  the OS; a small tool/integration layer uses `platform_file_read` and `platform_file_write` for
  persistence.
- The replay header includes magic, version, `sim_logic_hash`, seed, tick rate, and player count.
  Unknown versions or logic hashes fail loudly rather than being guessed compatible.

## Ranked slices

| ID | Slice | Done-when | Status |
|----|-------|-----------|--------|
| S0 | Restore the shared simulation constants contract | `core/sim_config.h` exists, consumers compile against it, and a focused test proves 30 Hz/Q16.16 values without duplicate definitions | queued |
| S1 | Scaffold platform-free `eng_sim` and placeholder world | CMake exposes `eng::sim`; `SimWorld` owns fixed-order SoA position/velocity/health/cooldown arrays plus tick and PCG32 state; `sim_init(seed)` and `sim_tick(world, commands)` are deterministic | queued |
| S2 | Add canonical state hashing | FNV-1a hashes every live gameplay field and RNG word byte-wise in documented order; changes to each field affect the hash; padding/capacity do not | queued |
| S3 | Add replay codec and platform-file persistence seam | Header and per-tick commands round-trip byte-exactly; malformed/truncated/oversized input is rejected; a focused tool or integration test writes and reads through the platform file API | queued |
| S4 | Prove 10,000-tick determinism and exact divergence | Two same-seed/input runs match every tick in Debug and Release; a controlled perturbation at tick N reports N as the first divergence | queued |
| S5 | Enforce the boundary and close M3.0 | Debug/Release `/WX` and CTest pass; sim source has no float, unordered iteration, wall-clock, platform, or renderer dependency; ROADMAP/JOURNAL/next-session are current | queued |

## Verification matrix

- Configure: `cmake --preset dev` and `cmake --preset ci`.
- Build: `cmake --build --preset debug` and the corresponding Release CI build.
- Tests: `ctest --test-dir build -C Debug --output-on-failure` plus Release.
- Determinism: same seed + same command bytes => identical hash at every one of 10,000 ticks.
- Negative proof: perturb one gameplay field at a selected tick => first mismatch is that tick.
- Replay: memory round-trip plus platform-file round-trip; corrupt magic/version/logic hash/length
  each fail deterministically.
- Isolation: link graph and source scan confirm `eng_sim -> eng_core + eng_math` only.

## Exit gate

M3.0 closes only when the self-check and deliberate-divergence test pass in both Debug and Release,
the replay persistence path is verified through the platform API, and CI is green. Then open a
separate M3.1 ECS slate that uses this hash stream as its regression oracle.

## Residuals

- M3.1 sparse-set ECS and entity generations.
- M3.2 system scheduling and ascending-entity ordered iteration.
- M3.3 fixed-tick accumulator and presentation snapshots.
- M3.4 full library isolation/float lint across the mature sim surface.
