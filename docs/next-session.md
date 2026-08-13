# MOBA-proto — next session

## State @ PR #38 acceptance head · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.4 is acceptance-complete. M3.4 lives on
`codex/m3.4-structural-determinism` in PR #38; the owner-approved squash merge is the remaining state
transition. Do not begin Phase 4 from this branch or from stale local `main`.

## M3.4 delivered

- `moba_sim_determinism` is the single `/fp:precise` owner for all authoritative sim translation
  units. The generated compile database proves nine sources × three MSVC configurations, one object
  owner, one policy marker, one compatible FP option, and only sim/core/math/serialize include roots.
- `moba_enforce_sim_target` fails CMake configure on unexpected implementation sources, include
  roots, direct links, or exported links. Source and negative-fixture scans reject float/libm,
  wall-clock, heap/unordered containers, platform/render/game imports, path traversal, system-include
  injection, and accidental sim-source recompilation.
- `sim_oracle_run` is platform-free and caller-storage-backed. The direct test probe and both
  Vulkan/null sandbox binaries call the same `eng_sim` archive through `--sim-self-check` and emit
  an identical 10,000-tick result: 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`.
- The clang-cl stretch script locates an installed compiler, proves UBSan with a real overflow
  tripwire, then runs the instrumented RelWithDebInfo oracle under `/WX`, no RTTI/exceptions, and the
  same sim policy. Local clang-cl 19.1.5 + UBSan and the hosted capability job are green.
- Both sandbox variants stage the Debug-ASan runtime app-locally, so parity remains executable under
  the sanitizer tree. No authoritative behavior, replay byte, schedule, or logic hash changed.

## Verified

- Full MSVC `/WX` Debug, RelWithDebInfo, and Release builds: 32/32 CTest entries in each.
- Debug-ASan: 32/32 after the sandbox runtime-stage repair.
- The 10,000-tick oracle and exact `tick=4321 field=position_x entity=7` divergence remain green.
- Local Vulkan run: RTX 4070 Ti, validation enabled, exactly 90 frames, clean exit, 2,764,854-byte
  1280×720 BMP accepted by the strict classifier.
- Exact-checkpoint fresh clone: configure, complete Debug build, 32/32, 90-frame screenshot, clean
  worktree. Hosted Debug/RelWithDebInfo/Release, clang-cl/UBSan capability, fresh-walk, CodeQL C/C++,
  and workflow analysis are green.
- Boundaries remain `eng_sim → core + math + serialize`; renderer and presentation/platform cadence
  do not enter the authoritative island.

## First action

1. Confirm PR #38 still points at the recorded ready head and every exact-head check is green.
2. Owner squash-merges PR #38, verifies the recorded head was merged, then fast-forwards local
   `main` and waits for post-merge CI/CodeQL.
3. Only from that synchronized green `main`, open a separate Phase 4 M4.0 asset-pipeline slate.

## Next slate boundary

M4.0 should define the asset identity/lifetime seam and direct trivial loaders before cooker breadth.
Keep source assets, baked assets, GPU upload ownership, and hot-reload generations explicit. Do not
fold gameplay, netcode, generic streaming, compression, or renderer redesign into the first asset
slice; preserve the M3.4 oracle as the regression gate throughout Phase 4.
