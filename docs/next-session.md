# MOBA-proto — next session

## State · `moba/slate-2026-08-11` · 2026-08-12

Phase 2 M2.3–M2.5 is complete on the slate branch. The Vulkan renderer now shows a
depth-correct 20×25 cube field from one batch/draw, camera UBO motion, debug world geometry and a
5×7 stroke F1 overlay. The public seam is typed resource creation/destruction plus
begin/submit/end-frame; deferred destruction and a real null backend are covered headlessly.

## Verified

- MSVC `ci` preset (`/WX`) builds Vulkan, null backend, both sandboxes, and tests.
- 9/9 CTest suites green.
- Validation-layer readback run: zero warnings/errors; 500 objects, one batch, one scene draw,
  three total draws, 12 live dedicated allocations.
- Owner S6 run passed: resize, minimize/restore, alt-tab focus loss/return, and F1 off/on were
  event-logged; validation remained clean through shutdown.
- `sandbox_null --frames 5` opens, creates valid typed handles, runs blank, and exits cleanly.

## First action

Review and squash-merge PR #13. Do not start Phase 3 implementation on the renderer branch.

## Phase 3 handoff

Create `moba/slate-phase3-determinism` from the updated `main` and execute the queued
[`M3.0 determinism slate`](slate-moba-phase3-m3.0.md). It restores the missing shared sim constants,
then builds the platform-free placeholder sim, canonical state hash, replay path, and 10,000-tick
exact-divergence proof. Keep M3.1 ECS in its own later slate; do not pull Phase 4 assets, allocator
Phase 2, or Vulkan-in-hosted-CI into M3.0.

## Residual owner gates

- Vulkan SDK installation in hosted CI before changing `find_package(Vulkan)` to `REQUIRED`.
