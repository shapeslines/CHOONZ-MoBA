# MOBA-proto — next session

## State · `moba/slate-phase3-determinism` · 2026-08-12

Phase 3 M3.0 is complete on PR #14. The branch contains the 30 Hz shared configuration,
allocation-free little-endian codec, platform-free 64-slot placeholder simulation, canonical
FNV-1a state hash and first-field diff, version-locked replay format, and the `moba_replay`
record/inspect/verify CLI. M3.1 ECS has not started.

## Verified

- MSVC `ci` preset (`/WX`) builds all Debug and Release targets.
- 18/18 CTest entries pass in both configurations.
- Independent 10,000-tick replay hash streams match; final hash `0xb85d4b632571948c`.
- Controlled mutation reports exactly `tick=4321 field=position_x unit=7`.
- CLI record → inspect → verify passes through atomic platform-file persistence; corrupt and
  incompatible variants return the specified exit classes.
- `eng_sim` source boundary is clean and links only core, math, and serialize.

## First action

Review and merge PR #14 once GitHub CI and CodeQL are green. Merge remains owner-approved and is not
part of the implementation slate.

## M3.1 handoff

After PR #14 lands, create `moba/slate-phase3-ecs` from updated `main` and execute
[`M3.1 ECS slate`](slate-moba-phase3-m3.1.md). Preserve the M3.0 replay format and 10,000-tick hash
stream as the regression oracle. M3.1 owns entity lifecycle and sparse-set SoA storage only; keep
M3.2 systems/scheduling, M3.3 fixed-loop/presentation, assets, networking, and gameplay out.

## Residual owner gate

- Vulkan SDK installation in hosted CI before changing `find_package(Vulkan)` to `REQUIRED`.
