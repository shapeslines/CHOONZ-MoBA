---
type: record
title: "SLATE - Phase 3 M3.4 structural determinism"
kind: slate
status: queued
project: moba
axis: "simulation build isolation"
queued: 2026-08-13
updated: 2026-08-13
worktree: "unassigned"
branch: "unassigned"
tags: [slate, simulation, build, determinism, isolation]
related:
  - "docs/ROADMAP.md M3.4"
  - "docs/slate-moba-phase3-m3.3.md"
  - "PR #28 / M3.3 acceptance repair"
---

# Slate - Phase 3 M3.4 structural determinism

**Status:** queued. Corrective PR #28 is squash-merged from exact green head `8cc42c1` as `b03f545`,
and post-merge `main` is green. Begin with S0 baseline capture; Phase 4 remains blocked until this
slate closes.

## Goal

Turn the already-behavioral determinism contract into one auditable build contract: central sim
compiler policy, identical game/test consumption of the sim binary, stronger dependency/source
isolation, and an explicit second-toolchain stretch proof. This milestone must not change gameplay,
replay encoding, tick order, or presentation behavior.

## Loop contract

Every slice states one observable done-condition, implements the smallest complete vertical change,
runs focused plus affected gates, fixes red before advancing, records evidence, and commits/pushes a
green checkpoint. Open a draft PR after the first implementation slice; mark it ready only after the
full local and exact-head GitHub gates pass. Merge remains separately owner-approved.

## Entry gate

- [x] PR #28 is squash-merged from recorded exact head `8cc42c1` as `b03f545`; local and origin
  `main` agree.
- [x] Post-merge Debug, RelWithDebInfo, Release, fresh-walk, and CodeQL checks are green.
- [ ] Capture the untouched M3.3 10,000-tick oracle `0x637628abff59c823`, replay logic hash
  `0xab96814425ba80a4`, and all four current dependency seams before build changes.
- [ ] Create a new M3.4 branch/worktree from that updated `main`; do not reuse the M3.3 branch.

## Invariants and fence

| May touch | Must not |
|-----------|----------|
| CMake compiler-policy targets/includes for deterministic code | authoritative sim schedule, commands, state, hash order, or replay layout |
| game/test executable parity harnesses | M4 asset implementation or Phase 5 gameplay |
| source/dependency/compile-command boundary checks | renderer, presentation, platform cadence behavior |
| clang-cl/UBSan configure and evidence scripts | force-pushes, rebases, or replacement of the MSVC required gate |

- `eng_sim` remains a platform-free static library with direct dependencies only on core, math, and
  serialize. Renderer and presentation remain downstream-only.
- One named CMake policy owns deterministic compile flags; no executable silently recompiles sim
  sources with different options.
- The test and runnable game paths consume the same `eng_sim` artifact and reproduce the same replay
  hash stream. Parity compares behavior, not object-file bytes or incidental linker layout.
- The existing MSVC Debug/RelWithDebInfo/Release matrix stays blocking. clang-cl/UBSan is a recorded
  stretch gate until toolchain support is proven; unsupported sanitizer combinations must report an
  explicit capability result rather than silently passing.
- `SIM_LOGIC_HASH` and replay format remain unchanged because build isolation is non-authoritative.
  Any observed behavior change stops the slate for review.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Land and rebaseline | branch starts at merged PR #28 and untouched M3.3 matrix/oracle/boundaries are recorded | queued |
| S1 | Central compiler policy | one CMake-owned deterministic policy applies to every `eng_sim` source in every configuration, with a test that detects drift | queued |
| S2 | Binary-path parity | test and runnable game paths consume the same sim library and replay the same commands to the exact M3.3 hash stream | queued |
| S3 | Strong isolation lint | source, include, link, compile-command, and accidental-source-recompile checks fail on forbidden sim/platform/render/presentation coupling | queued |
| S4 | Second-toolchain stretch | clang-cl config builds the oracle and UBSan runs where supported, with exact capability/evidence recorded | queued |
| S5 | Close M3.4 | `/WX` matrix, ASan, parity, boundary scans, fresh walk, docs, independent acceptance, and exact-head GitHub gates are green | queued |

## Exit gate

M3.4 closes only when deterministic flags have one owner, game/test paths prove the same M3.3 oracle
through the same sim artifact, isolation drift is mechanically rejected, and all blocking MSVC gates
remain green. The clang-cl/UBSan result must be explicit and reproducible. Only then may Phase 4 open.
