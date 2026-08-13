# Phase 4 M4.0 Slate - Direct Assets and Lifetime Registry

**Status:** active

**Branch:** `codex/m4.0-assets`

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
- [ ] Route the sandbox texture through `eng_assets` and prove unload ownership.
- [ ] Full Debug/RelWithDebInfo/Release, ASan, oracle, boundary, fresh review, and PR.

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
