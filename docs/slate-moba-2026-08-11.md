---
type: record
title: "SLATE — Phase 2 pickup: M2.3–M2.5 (instanced meshes → seam audit)"
kind: slate
status: active
project: moba
axis: "renderer bring-up (Phase 2 milestones)"
opened: 2026-08-11
updated: 2026-08-12
closed:
worktree: "repo root"
branch: "moba/slate-2026-08-11"
tags: [slate, orthogonal, planning]
related:
  - "[[docs/JOURNAL]] Session 05"
  - "vault: 20 Projects/moba/moba.md"
  - "docs/ROADMAP.md Phase 2"
---

# Slate moba-2026-08-11 — Phase 2 pickup: M2.3–M2.5

> One orthogonal slate = a ranked set of **fence-disjoint** slices for one period/axis.
> Altitude: charter ⊇ **slate** ⊇ slice ⊇ TaskSpec. One writer per fence; owner gates named, never skipped.

**Opened:** 2026-08-11 · **Status:** active
**Axis:** renderer bring-up — M2.3 (camera UBO + instanced meshes) → M2.4 (debug-draw + overlay) → M2.5 (seam audit + null backend)
**Orthogonal to:** Phase 3+ (sim), Phase 4 (assets), netcode; no other session touches this repo.

## State at refresh

- **Tip / head:** `moba/slate-2026-08-11` (implementation complete; final commit/PR pending)
- **Open PRs:** none — MOBA-proto is a **single-claimant** repo this period
- **In-flight fences:** this root checkout only; the stale external worktree was removed
- **CI / merge gate:** GitHub Actions Debug×Release `ci` (/WX) + `ctest --output-on-failure --no-tests=error`; local `tools/hooks/ctest-gate.bat` on push

## Why

Vault claim (2026-08-11): resume Phase 2 starting M2.3 — M2.2 done, M2.3–M2.5 remain.
This slate carries the handoff: the first "MOBA-looking" frame (per-frame view/proj UBO at the
`set=0` slot reserved in M2.2, per-instance buffer, 500+ instances in one batched draw), then
the debug/overlay workhorse, then the seam audit that makes the renderer API safe for Phase 4.
Each slice has an observable done-when; the milestone DoDs are the slice gates. Work runs in a
session-fenced worktree so `main` stays green.

## Fence

| May touch | Must not |
|-----------|----------|
| `engine/render/**` (renderer.h seam, pipeline registry, descriptors, buffers, dispatch) | `engine/sim`, `engine/net`, `engine/assets` (Phases 3/4/6 — don't exist yet) |
| `engine/render/shaders/` (new vert/frag per rung, offline SPIR-V per ADR-0008) | `docs/DECISIONS` additions only when a seam change requires an ADR (then cite it) |
| `tools/sandbox/**` (camera input, wire-up) | Vulkan SDK-in-CI flip (residual) |
| `tests/**` (headless suites only — no Vulkan in tests) | clang-cl/UBSan, ASan preset, `plat_mem_*` split (residuals) |
| `docs/` (ARCHITECTURE, ROADMAP, JOURNAL, this slate — same commit as the change) | Any other repo; vault writes limited to `20 Projects/moba/moba.md` pointer |
| `engine/core`, `engine/math`, `engine/platform` — only if a rung strictly requires it | |

## Ranked slices

| ID | Slice | Gate | Done-when | Status |
|----|--------|------|-----------|--------|
| S1 | M2.3-a: per-frame view/proj UBO at `set=0` (persistent-mapped HOST_VISIBLE\|HOST_COHERENT ring, written each frame, no staging) | Agent | Camera moves via sandbox input and the UBO reflects it; `set=1` material layout untouched; validation clean | done `b87e653` |
| S2 | M2.3-b: storage-buffer instances + deterministic batched draw | Agent | 500 cubes render from **one** `vkCmdDrawIndexed(instanceCount=N)` | done 2026-08-12 |
| S3 | M2.3-c: DoD verification + milestone record | Agent | Readback screenshot; `ci` (/WX) and tests green; milestone docs current | done 2026-08-12 |
| S4 | M2.4: debug-draw + F1 overlay | Agent | World lines/text visible; frame-arena reset tested; validation clean | done 2026-08-12 |
| S5 | M2.5: typed handles, deferred destroy, null backend | Agent | Vulkan/null build together; null sandbox/tests green; no Vulkan leak above seam | done 2026-08-12 |
| S6 | Interactive DoD: zero validation errors across real resize / minimize-restore / alt-tab (needs a display) | Owner | Human runs `sandbox.exe`; zero validation messages across all three interactions | blocked |

## Concurrency map

- **Immediately, in parallel:** none — one writer, one repo, sequential renderer surface.
- **Sequenced:** S1 → S2 → S3 (same TU surface, rungs of one milestone) → S4 → S5 (M2.5 folds the provisional M2.2 upload seam) → S6 anytime after S2 (owner).
- **Owner-gated (await):** S6 (display), plus CI SDK flip and any vault-PM mutation.
- **Ordering rule:** Phase 2 rungs are strictly ordered by ROADMAP dependency; do not start S2 until S1's done-when is observably true.

## Owner gates

| Item | OWNER |
|------|-------|
| S6 interactive DoD (resize / minimize / alt-tab on a real display) | Carson |
| Vulkan SDK install in CI → `find_package(Vulkan) REQUIRED` | Carson |
| Any ADR-level decision (new DECISIONS file) surfaced mid-slate | Carson |

## Merge gate

`ci` preset (/WX) build clean + `ctest --output-on-failure` green (incl. `--no-tests=error`)
+ pre-push hook. Validation layer: zero messages on Vulkan runs. Land via squash PR to `main`
per repo history (PRs #2–#10 pattern); docs updated in the same commit as the change.

## Recommended next

**Rank-1 (owner): S6** — run resize, minimize/restore, and alt-tab against the ready
sandbox and confirm zero validation messages. Agent implementation work is complete.

## Non-goals / residual after close

- Phase 3+ milestones (sim, assets, netcode) — later phases, own slates
- Vulkan SDK in CI (`find_package` → REQUIRED) — deferred, owner-gated
- `plat_mem_*` split out of the Win32 window TU; clang-cl/UBSan determinism run; Debug-ASan preset; OpenCppCoverage HTML
- Interactive DoD for M2.0–M2.2 (same owner-display gate, S6 pattern)
- M4.2+ (own PNG inflate), hot-reload, `.mba` container — Phase 4

## Revisions

- 2026-08-11 · authored · opencode deepseek-v4-flash session
- 2026-08-12 · S2–S5 implemented and verified; S6 remains owner-gated
