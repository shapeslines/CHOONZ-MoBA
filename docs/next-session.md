# MOBA-proto — next session

## State @ merged `b03f545` · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.3 is complete on merged `main`. PRs #21–#24 landed in order; corrective PR #28
preserved that history, passed independent acceptance, and squash-merged exact head `8cc42c1` as
`b03f545`. Post-merge CI and CodeQL are green. M3.4 has only a queued slate; no M3.4 implementation
has begun.

## Landed stack

- PR #21 `04ae177` → `8defa10`: Wave 3 plus superseded Wave 2b content and strict hosted Vulkan gate.
- PR #22 `d35adee` → `4d250b2`: renderer robustness and restored Vulkan hard-require gate.
- PR #23 `6a39c17` → `ab774ed`: ADR-0014 and roadmap ownership amendments.
- PR #24 `c56ade4` → `ca5ad22`: initial M3.3 wave, corrected forward by PR #28.
- PR #28 `8cc42c1` → `b03f545`: platform cadence, transactional snapshots, boundary repair, strict
  hosted Vulkan classification, Windows ASan CTest runtime staging, and final evidence/docs.

## Verified locally

- `/WX` Debug, RelWithDebInfo, and Release builds: 28/28 CTest entries green in each.
- Debug-ASan: 28/28 green with the ASan runtime staged app-locally.
- Replay v1 and `SIM_LOGIC_HASH = 0xab96814425ba80a4` are unchanged.
- Two independent 10,000-tick runs end at `0x637628abff59c823`; mutation reports exactly
  `tick=4321 field=position_x entity=7`.
- 60/30/mixed/minimized cadence groupings preserve ticks and hashes; two owed ticks generate two
  distinct same-tick command buffers; failed ticks retain time debt.
- Arena snapshot init is transactional; extraction is const and hash-neutral; actual live count and
  64 stable slots are preserved; interpolation is previous→current and identity-aware.
- Fresh-clone README walk and a 90-frame validation-clean RTX 4070 Ti screenshot are green.
- CI and fresh-walk share one smoke classifier; 14 adversarial cases prove device-gate phrases cannot
  mask nonzero exits, Vulkan validation WARN/ERROR diagnostics, incomplete runs, or missing/invalid
  screenshots.
- `eng_sim → core + math + serialize`; `eng_game → core + math + sim + render_common`; renderer has
  no sim dependency; platform cadence has no game/sim dependency.
- Exact-head and post-merge Debug/RelWithDebInfo/Release, fresh-walk, CodeQL C/C++, and workflow
  analysis are green. Remote wave and corrective branches were retained.

## First action

1. Execute M3.4 slice S0 from updated `main`: capture the untouched M3.3 matrix, oracle, logic hash,
   and dependency seams before changing build policy.
2. Create a separate M3.4 branch/worktree only after that baseline is recorded. Do not reuse an M3.3
   branch and do not begin Phase 4.

## Next slate

Open [`M3.4 structural determinism`](slate-moba-phase3-m3.4.md) from updated green `main`. Centralize
sim compile flags, prove game/test binary parity, strengthen isolation linting, and run the
clang-cl/UBSan stretch gate where supported. Keep `SIM_LOGIC_HASH` unchanged unless authoritative
behavior changes. Phase 4 remains blocked until M3.4 closes.
