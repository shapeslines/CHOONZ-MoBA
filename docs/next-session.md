# MOBA-proto — next session

## State · `moba/slate-2026-08-11` · 2026-08-12

Phase 2 M2.3–M2.5 implementation is complete on the slate branch. The Vulkan renderer now shows a
depth-correct 20×25 cube field from one batch/draw, camera UBO motion, debug world geometry and a
5×7 stroke F1 overlay. The public seam is typed resource creation/destruction plus
begin/submit/end-frame; deferred destruction and a real null backend are covered headlessly.

## Verified

- MSVC `ci` preset (`/WX`) builds Vulkan, null backend, both sandboxes, and tests.
- 9/9 CTest suites green.
- Validation-layer readback run: zero warnings/errors; 500 objects, one batch, one scene draw,
  three total draws, 12 live dedicated allocations.
- `sandbox_null --frames 5` opens, creates valid typed handles, runs blank, and exits cleanly.

## First action

Run the owner-only S6 interaction check in `build\tools\sandbox\Debug\sandbox.exe`: resize the
window, minimize/restore, and alt-tab; confirm no validation messages. Then mark the slate closed and
land the branch via squash PR.

## After Phase 2 closes

Open a fresh Phase 3 slate starting at M3.0 (determinism harness/state hash), then M3.1 ECS. Do not
pull Phase 4 assets, allocator Phase 2, or Vulkan-in-hosted-CI into that slate.

## Residual owner gates

- S6 interactive Vulkan lifecycle check.
- Vulkan SDK installation in hosted CI before changing `find_package(Vulkan)` to `REQUIRED`.
