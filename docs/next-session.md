# MOBA-proto — next session

## State @ `1624070` (main) · `b87e653` (moba/slate-2026-08-11) · 2026-08-11 · doto-n/opencode

Phase 2 pickup: M2.3-a (S1) landed on branch `moba/slate-2026-08-11` (worktree
`.worktrees/moba-slate-2026-08-11`) — per-frame view/proj UBO at set=0 + sandbox orbit
camera. Slate: `docs/slate-moba-2026-08-11.md`. Main is clean; branch is pushed, no PR yet.

## Shipped
- `1624070` docs(slate): Phase 2 pickup slate M2.3–M2.5 [main, pushed]
- `b87e653` feat(render): M2.3-a UBO at set=0 + orbit camera [moba/slate-2026-08-11, pushed]

## Signals
- **state/flags:** Vulkan SDK 1.4.357.0 installed on THIS machine (winget) — set
  `VULKAN_SDK=C:\VulkanSDK\1.4.357.0`; build via `vcvars64` + `cmake --preset dev`.
  Repo's other machine (Carson) had VS18 Enterprise + SDK; here it's VS2022 Community.
  `docs/next-session.md` created (this file — first one).
- **communicated:** none needed (single-claimant repo this period).
- **raised for /custodian:** none — no `docs/custodian-queue.md` in this repo yet.
- **FOR /brain:** distill → brain/moba-engine-proto.md ← MOBA-proto@b87e653:
  design docs at vault `20 Projects/moba/` (+ new divergence register
  `moba_engine_reconciliation.llm.md`); repo ADRs are implementation authority;
  S1 proved set=0 UBO ring pattern (per-frame slot write after fence wait, HOST_COHERENT).
- **DEFERRED / unresolved:** JOURNAL Session 06 entry (M2.3-a) not yet written (belongs to
  S3); slate statuses not yet marked (S1 → done); no PR opened for `moba/slate-2026-08-11`
  (merge gate = squash PR, per repo history); interactive resize/min/alt-tab DoD (S6, owner).

## Next — FIRST action
1. In worktree `.worktrees/moba-slate-2026-08-11`: implement S2 — M2.3-b per-instance
   buffer + push-constant model + one batched instanced draw (500+ cubes).
   Order: S1 → S2 → S3 → S4 → S5 (see slate); S6 owner-gated.

## Queue
- S2 M2.3-b instanced draw (needs `CmdPushConstants` in `src/vk/vk.h` dispatch — absent)
- S3 M2.3-c DoD verify + JOURNAL Session 06
- S4 M2.4 debug-draw + F1 overlay
- S5 M2.5 seam audit (typed handles, deferred-destroy, null backend)
- S6 interactive DoD (owner: real display)
- Residual: Vulkan SDK in CI (`find_package` → REQUIRED), `plat_mem_*` split, clang-cl/UBSan
