# MOBA-proto — next session

## State @ PR #51 repaired local acceptance · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.4 is complete on `main` at `63f0b1de`. The active interphase security
consolidation is `codex/security-consolidation` / draft PR #51. Candidate `a89caf6` passed its full
local and GitHub gates but failed the required security review, so it was not promoted. All three
findings now pass the complete local matrix at code checkpoint `f0a56c4`; fresh full reviews and
exact-head GitHub checks remain. Merge is still a separate owner decision. Do not begin M4.0 from
this branch.

## Consolidation delivered

- PR #27 and PRs #29–#46 are accounted for in
  [`security-consolidation-ledger.md`](security-consolidation-ledger.md); every approved source hunk
  is retained or strengthened. PR #26 is stale/superseded. PRs #47–#50 and the unfinished platform
  memory worktree remain explicitly outside this slate.
- CI has `workflow_dispatch`, `contents: read`, three immutable checkout v5.0.1 pins, and explicit
  winget community-source selection. Fresh-walk deletion is constrained to a non-root direct temp
  child and rejects files, roots, outside/nested paths, source-self targets, and reparse points.
- Core Array/arena/HashMap arithmetic and Pool/HandlePool free-list state are guarded before mutation.
  Platform atomic writes refuse pre-existing `.tmp` files; Vulkan and shader paths are restricted and
  validated.
- Sandbox/replay/test/visualizer parsing is canonical and side-effect-free on failure. Classifier logs
  are bounded at 4 MiB; renderer capacity arithmetic rejects overflow/corrupt state before reads or
  mutation.
- Replay v1, command bytes, authoritative sim, asset APIs, and
  `SIM_LOGIC_HASH = 0xab96814425ba80a4` are unchanged.

## Verified locally

- `/WX` Debug, RelWithDebInfo, and Release builds; 39/39 CTest entries in every configuration.
- Debug-ASan 39/39; clang-cl 19.1.5 + UBSan 6/6. The visualizer now stages its ASan runtime
  app-locally, closing the one DLL-discovery failure found by the first sanitizer pass.
- Direct oracle: 10,000 ticks, 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`; controlled divergence remains exactly
  `tick=4321 field=position_x entity=7` (178,690 checks).
- Guarded fresh clone: 92-target Debug build, 39/39, and 90-frame `SANDBOX_SMOKE=PASS`.
- Local Vulkan: RTX 4070 Ti, Vulkan 1.3, validation on, clean 90-frame exit, visually inspected
  2,764,854-byte 1280×720 screenshot.

## First action

1. Publish the local-acceptance evidence record.
2. Obtain fresh security and acceptance verdicts and require that exact head's MSVC matrix,
   clang-cl/UBSan, fresh-walk, and both CodeQL checks pass;
   then mark PR #51 ready.
3. Stop for separate owner merge approval. After an exact-head squash merge, verify the merged tree
   and `main` run (manual dispatch only if automatic push registration fails), then close
   #26/#27/#29–#46 with the landing SHA. Retain all source branches/worktrees.
4. Only from synchronized green `main`, open a separate Phase 4 M4.0 asset-pipeline slate.

## M4.0 boundary

M4.0 defines asset identity/lifetime and the direct TGA/WAV/SPIR-V loaders before cooker breadth.
Keep source/baked assets, GPU upload ownership, and hot-reload generations explicit. Do not fold
gameplay, networking, generic streaming, compression, or renderer redesign into the first asset
slice; keep the Phase 3 oracle and structural/security gates permanent.
