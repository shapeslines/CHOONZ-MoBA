# MOBA-proto — next session

## State @ M4.0 final local acceptance · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.4 and the interphase security consolidation are merged on `main`. PR #51 exact head
`e516b46` landed as `a2565ca`, which is the synchronized base for the isolated
`codex/m4.0-assets` worktree. Draft PR #52 contains only M4.0; the dirty historical root checkout was
not modified.

Implementation is complete; the final security repair is locally accepted and awaits its exact-head
commit/check identity:

- `eng_assets` owns portable normalized-path FNV-1a/64 `AssetId`, a fixed-capacity arena-backed
  generational SoA registry, stable path lookup with collision rejection, level bulk-unload, and a
  small refcounted global lifetime. Win32 aliases and device stems are rejected before hashing.
- Direct TGA supports type 2/type 10, 24/32-bit true-color and both vertical origins; direct WAV
  accepts the strict RIFF PCM subset (1–2 channels, 8/16/24/32-bit). Validation completes before
  durable allocation or registry mutation.
- Loose loads size/read one handle-bound file below a handle-bound non-reparse asset root and reject
  reparse and hard-link aliases. Registry initialization rejects overlapping backing/control ranges;
  initialization and failed loads preserve all caller arenas and registry state.
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
- All eight GitHub CI/CodeQL checks passed on earlier head `acc60b4`. The final reviewed security
  repair head must pass the same exact-head gate before PR #52 becomes ready.
- Exact head `82c4213` passed local acceptance but stayed draft after full security review found a
  fixed-path pre-delete in one asset test. The shared fixture now atomically creates its scope and
  child, binds their identities through handle-based cleanup, and proves both names cannot be
  replaced while live. Focused rereview and Debug 43/43 pass; its successor head is the only landing
  candidate.

## First action

1. Commit and push the final M4.0 security/evidence closure and require all exact-head GitHub checks.
2. Obtain fresh exact-head security and acceptance verdicts; mark PR #52 ready only when local and
   hosted evidence agree.
3. Owner squash-merge authorization is recorded. Merge only the recorded exact green head, verify
   the merged tree, and synchronize `main`.
4. Open a separate M4.1 slate from that green `main`; do not stack cooker work on PR #52.

## M4.1 boundary

M4.1 is the offline cooker plus one versioned little-endian `.mba` container. Keep the shared parser
surface POD-only, emit byte-identical output from repeated full cooks, add build-time AssetId
collision diagnostics/generated constants, and make the runtime reject bad magic/version/type/size
before allocation. The first vertical proof is TGA → `.mba` texture → the existing registry/upload
seam. No PNG inflate, glTF, incremental dependency graph, pack file, compression, hot reload,
gameplay, or networking belongs in that slate.

---
## ⚑ Fleet audit flag — 2026-08-18 (tier-0 pass; uncommitted rider)
Full report: `GITHUB-ROOT/_SCRATCH/fleet-audit-2026-08-18/REPORT.md`
- Behind 2 docs-only commits (08-18 re-scribe PR #56) — pull. Twin clone MOBA-proto (same remote+HEAD) hosts 2 prunable Temp worktrees; consolidate when M4.x lands.
- Grades B/B/B/A/A/B → B+ · Triage: CONTINUE (close PR #52 exact-head gate).
