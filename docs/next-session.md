# MOBA-proto — next session

## State @ M3.3 acceptance repair · 2026-08-13 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.2 is merged. PRs #21–#24 also merged in order, but #23/#24 landed
externally before their acceptance corrections. Corrective PR #28 preserves that history and carries
the complete platform-cadence and presentation-boundary repair. M3.4 has only a queued slate; no
M3.4 implementation has begun.

## Landed stack

- PR #21 `04ae177` → `8defa10`: Wave 3 plus superseded Wave 2b content and strict hosted Vulkan gate.
- PR #22 `d35adee` → `4d250b2`: renderer robustness and restored Vulkan hard-require gate.
- PR #23 `6a39c17` → `ab774ed`: ADR-0014 and roadmap ownership amendments.
- PR #24 `c56ade4` → `ca5ad22`: initial M3.3 wave, corrected forward by PR #28.
- PR #28 `codex/m3.3-acceptance-repair`: platform cadence, transactional snapshots, boundary repair,
  Windows ASan CTest runtime staging, and final evidence/docs.

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
- CI and fresh-walk share one smoke classifier; 11 adversarial cases prove device-gate phrases cannot
  mask nonzero exits, validation diagnostics, incomplete runs, or missing/invalid screenshots.
- `eng_sim → core + math + serialize`; `eng_game → core + math + sim + render_common`; renderer has
  no sim dependency; platform cadence has no game/sim dependency.

## First action

1. If PR #28 is still open, compare its live head to the recorded accepted head, require exact-head
   Debug/RelWithDebInfo/Release CI plus CodeQL green, and squash-merge without deleting remote
   branches. If any head changed, repeat acceptance on that exact head.
2. Fast-forward local `main` and require post-merge CI/CodeQL green before opening new work.

## Next slate

Open [`M3.4 structural determinism`](slate-moba-phase3-m3.4.md) from updated green `main`. Centralize
sim compile flags, prove game/test binary parity, strengthen isolation linting, and run the
clang-cl/UBSan stretch gate where supported. Keep `SIM_LOGIC_HASH` unchanged unless authoritative
behavior changes. Phase 4 remains blocked until M3.4 closes.
