# Phase 4 M4.0 Slate - Direct Assets and Lifetime Registry

**Status:** final security repair locally accepted; draft PR #52 awaits exact-head checks/reviews

**Branch:** `codex/m4.0-assets`

**PR:** #52 — `Phase 4 M4.0: direct assets and lifetime registry`

**Base:** merged `main` at `a2565cafc89942b834b1fe310b72cf1acf9ec8d5`

## Goal

Land only M4.0: stable normalized-path asset identity, an arena-backed SoA registry,
direct TGA/WAV loading, the existing raw-SPIR-V contract, and sandbox texture upload
through the asset manager. M4.1 cooker/container work remains out of scope.

Every slice closes the same loop: state an observable goal, implement the smallest
vertical change, run focused tests plus affected builds, repair without widening,
record and commit the green checkpoint, then advance.

## Baseline evidence

- Synchronized green `main`; isolated worktree, dirty root checkout untouched.
- CI-preset Debug build: 92/92 targets.
- Debug CTest: 39/39.
- Oracle: 10,000 ticks, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`.

## Slice ledger

- [x] Land/rebaseline gate.
- [x] Stable `AssetId` plus transactional arena-backed registry and lifetime tests.
- [x] Promote TGA and add bounded WAV parsing/file loads.
- [x] Route the sandbox texture through `eng_assets` and prove unload ownership.
- [x] Full Debug/RelWithDebInfo/Release, ASan, oracle, boundary, and Vulkan acceptance.
- [x] Repair portable identity, arena-overlap, and rooted-file boundary findings; obtain two focused
  security passes and repeat the full local acceptance matrix.
- [ ] Commit the final record, pass that exact head's GitHub checks and fresh reviews, mark PR #52
  ready, then use the recorded owner authorization to squash-merge only that green head.

## Locked decisions

- ADR-0010 wins: FNV-1a/64 over lowercase normalized relative paths.
- Renderer owns GPU allocations. `eng_assets` receives a Vulkan-free upload/destroy
  callback and stores only typed handle bits.
- Level data rewinds in O(1); global data is a deliberately small refcounted set.
- Loose raw `.spv` remains renderer-owned per ADR-0008; no runtime GLSL and no M4.1
  container/cooker work enters this slate.

## Slice evidence

### Identity and registry

- CI-preset Debug `engine_tests` target builds warning-clean.
- Focused `asset_id` and `assets`: 2/2.
- Full Debug CTest after adding `eng_assets` to the headless aggregate: 41/41.
- Normalized relative paths, forged/stale handle rejection, level rewind,
  generation reuse, global refcounts, capacity failure, and transactional
  under-budget initialization are covered directly.

### Direct loaders

- `tools/sandbox/src/tga_direct.*` is removed; sandbox and tests consume
  `eng_assets`.
- TGA covers raw/RLE 24/32-bit true-color, both vertical origins, complete packet
  preflight, malformed run/truncation rejection, and allocation-free failure.
- WAV covers strict RIFF PCM, padded unknown chunks, 1-2 channels, integer
  8/16/24/32-bit samples, and rate/alignment consistency.
- Loose file loads use a bounded allocator during the open/read race, rewind the
  I/O arena after every outcome, and preserve durable state on failure.
- Focused asset/TGA/WAV tests: 4/4. Full Debug CTest: 42/42.

### Sandbox ownership

- `assets_boundary` proves `eng_assets` has no render/Vulkan/sim import and the
  sandbox no longer decodes TGA or reads its texture file directly.
- Real Vulkan smoke: RTX 4070 Ti, validation enabled, asset-managed 64x64 texture
  id `0x3d9bff0eddada061`, 64 objects, one scene draw, clean three-frame exit.
- Material teardown precedes registry shutdown; registry shutdown returns texture
  destruction through the renderer callback before renderer destruction.
- Focused asset/boundary/oracle gate: 6/6. Full Debug CTest: 43/43.

### Final local acceptance

- `/WX` Debug, RelWithDebInfo, and Release builds complete 97/97 targets; every configuration passes
  43/43 CTest entries.
- Debug-ASan completes 97/97 targets and 43/43 tests. clang-cl 19.1.5 plus UBSan passes its runtime
  tripwire and 6/6 deterministic/isolation tests.
- Direct Debug and Release 10,000-tick runs both report 923 commands, final
  `0x637628abff59c823`, stream `0x6f381609f7e59f0c`, and unchanged logic
  `0xab96814425ba80a4`.
- RelWithDebInfo Vulkan acceptance completes 90 frames on an RTX 4070 Ti with validation enabled,
  the asset-managed 64×64 texture id `0x3d9bff0eddada061`, 64 objects, one scene draw, and a visually
  inspected 1280×720 screenshot.
- Implementation checkpoints are `ad4d3da` (identity/registry), `7a1c352` (bounded direct loaders),
  and `dc5e208` (sandbox integration). All eight CI/CodeQL checks on `dc5e208` passed; the final
  documentation head must earn the same exact-head result before readiness.

### Security repair loop

- Fresh full review found lexical-only root containment, Win32 filename aliases, and independently
  backed `Arena` controls that could still overlap one memory range. The candidate remained draft.
- Asset identity now accepts only lowercase portable `[a-z0-9._-]` components and rejects device
  stems. Registry initialization exhaustively rejects backing/backing, control/control, and
  control/backing overlap before mutation; exact adjacency remains valid.
- `platform_file_read_rooted` binds the root and every descendant without following reparses, keeps
  the chain live through the read, rejects multi-link files, and obtains size and bytes from the same
  final handle. The asset loaders cannot regress to stat plus ordinary read under boundary lint.
- Real junction and hard-link fixtures live only in create-new uniquely owned directories with held
  custody handles and exact nonrecursive teardown. Two independent focused security rereviews pass.
- Exact head `82c4213` passed fresh acceptance but full security review found one remaining test-only
  fixed-path pre-delete. The asset and platform fixtures now share a helper that atomically creates
  both its scope and working child, holds their custody handles, re-verifies the child's file ID at
  handle-based disposition, and disposes the scope through its original creation handle. A hostile
  regression proves neither name can be replaced while bound. Focused rereviews pass; Debug
  assets/platform pass 25 cases and 326 checks, Debug CTest passes 43/43, and no fixture path remains.
  `82c4213` and `c31bd1b` are superseded and unmergeable.
- Final repaired tree: `/WX` Debug, RelWithDebInfo, and Release 43/43 each; Debug-ASan 43/43;
  clang-cl/UBSan 6/6; Debug/Release oracle unchanged; RTX 4070 Ti 90-frame validation-on screenshot
  passes through the rooted loader. Exact-head hosted checks and final acceptance remain.
