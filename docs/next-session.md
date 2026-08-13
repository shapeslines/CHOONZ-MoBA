# MOBA-proto — next session

## State @ M4.0 final local acceptance · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.4 and the interphase security consolidation are merged on `main`. PR #51 exact head
`e516b46` landed as `a2565ca`, which is the synchronized base for the isolated
`codex/m4.0-assets` worktree. Draft PR #52 contains only M4.0; the dirty historical root checkout was
not modified.

Implementation is complete at checkpoint `dc5e208`:

- `eng_assets` owns normalized-path FNV-1a/64 `AssetId`, a fixed-capacity arena-backed generational
  SoA registry, stable path lookup with collision rejection, level bulk-unload, and a small
  refcounted global lifetime.
- Direct TGA supports type 2/type 10, 24/32-bit true-color and both vertical origins; direct WAV
  accepts the strict RIFF PCM subset (1–2 channels, 8/16/24/32-bit). Validation completes before
  durable allocation or registry mutation.
- Loose loads use a bounded allocator in the rewindable I/O arena, closing the stat/open growth
  race. Initialization and failed loads preserve all caller arenas and registry state.
- The renderer still owns GPU memory. `eng_assets` has no Vulkan/render/sim dependency and receives
  only a Vulkan-free upload/destroy callback. The sandbox loads `uv_test.tga` through the registry
  and tears down materials/assets before the renderer.
- Raw loose `.spv` remains renderer-owned under ADR-0008. No `.mba`, cooker, PNG, glTF, hot reload,
  gameplay, networking, or simulation behavior entered M4.0.

## Verified locally

- CI-preset `/WX` Debug, RelWithDebInfo, and Release builds: 97/97 targets in each configuration;
  CTest: 43/43 in each configuration.
- Debug-ASan: 97/97 targets and 43/43 tests.
- clang-cl 19.1.5 plus UBSan: runtime capability tripwire and 6/6 deterministic/isolation tests.
- Debug and Release oracle: 10,000 ticks, 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`.
- RTX 4070 Ti Vulkan 1.3, validation enabled: 90 clean frames, asset-managed 64×64 texture id
  `0x3d9bff0eddada061`, 64 objects, one scene draw, and a visually inspected 1280×720 screenshot.
- All eight GitHub CI/CodeQL checks passed on implementation head `dc5e208`. The documentation
  closure commit must pass the same exact-head gate before PR #52 becomes ready.

## First action

1. Review and commit the M4.0 evidence/documentation closure, push it, and require all exact-head
   GitHub checks.
2. Obtain final acceptance and mark PR #52 ready only when local and hosted evidence agree.
3. Stop for the explicit owner decision on squash-merging PR #52. After an authorized merge, verify
   the recorded exact head landed and synchronize `main`.
4. Open a separate M4.1 slate from that green `main`; do not stack cooker work on PR #52.

## M4.1 boundary

M4.1 is the offline cooker plus one versioned little-endian `.mba` container. Keep the shared parser
surface POD-only, emit byte-identical output from repeated full cooks, add build-time AssetId
collision diagnostics/generated constants, and make the runtime reject bad magic/version/type/size
before allocation. The first vertical proof is TGA → `.mba` texture → the existing registry/upload
seam. No PNG inflate, glTF, incremental dependency graph, pack file, compression, hot reload,
gameplay, or networking belongs in that slate.
