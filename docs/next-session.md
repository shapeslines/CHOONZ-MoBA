# MOBA-proto — next session

## State @ main (after gap-close waves 0-6, 2026-08-12)

Phase 3 M3.0-M3.3 complete. M3.2 merged (`d9f5230`, tag `v0.3.0-m3.2`); M3.3 landed in
the gap-close Wave 6 (`eng_game` present glue). The 40-gap sweep (docs/gap-close-ledger.md)
is closed for waves 0-6; remaining open items are phase-owned (G11 clang-cl @ M3.4,
G13 /fp pinning @ M3.4, G20 parity @ M6.7, G34 fuzz @ M4.2/M6.1).

## Shipped (this sweep)

- Wave 0: PR #17 merged, tag, branch retirement, `main-gate` ruleset (required checks).
- Wave 1: docs truth (ADR misrefs, session retrospectives, LICENSE, templates).
- Wave 2a/2b: debug-asan preset (25/25 under ASan; poison gated to virtual arenas),
  /guard:cf, SDK 1.4.357.0 pin, Vulkan in CI + RelWithDebInfo matrix + sandbox
  validation run, perf ceiling, fresh-walk.ps1 (proven live).
- Wave 3: ADR-0013 RNG policy, ADR-0014 validation/backpressure, hash-coverage guard,
  full-capacity test, window-free platform TUs (engine_tests user32-free).
- Wave 4: DPI awareness, cache-header fuzz, allocator pressure warning, culling owner
  (M4.3), hardware-testing protocol.
- Wave 5: ownership amendments (moba_game glue @ M3.3, Raw Input @ M5.2, tga_direct @ M4.0).
- Wave 6: M3.3 complete — `engine/game` present glue, fixed-tick accumulator,
  snapshots, interpolation; sandbox runs a deterministic 64-unit orbit demo.

## Verified

- `/WX` Debug + Release: 26/26 ctest green (incl. `present`).
- Oracle: `0x637628abff59c823` (10k-tick), mutation `tick=4321 field=position_x entity=7`.
- ASan preset: 25/25 (the mem suite + full sim/render suites).
- Sandbox: validation-clean on RTX 4070 Ti; objects=64 batches=1; screenshot + clean exit.
- fresh-walk: FRESH-WALK OK end to end (clone -> build -> 25/25 -> screenshot).

## Signals

- state/flags: PRs #18-#24 (gap-close waves 1-6) are open, stacked in order
  w1 -> w2a -> w2b -> w3 -> w4 -> w5 -> w6; merge bottom-up as checks go green, then
  delete each branch. `main-gate` ruleset requires `windows-msvc (Debug)` +
  `(Release)`; after w2b lands, add `windows-msvc (RelWithDebInfo)` to the ruleset.
- communicated: M3.3's runtime demo content (orbit commands) is sandbox-app-only and
  independent of the oracle tests.
- raised for /custodian: `docs/gap-close-ledger.md` is the sweep ledger — sweep it
  against reality after the PRs merge (several gaps are "closed" in the ledger but the
  PRs are not yet merged).
- FOR /brain: `brain/moba-sim-present-boundary.md` <- M3.3: accumulator, snapshot
  extraction, the single fixed->float owner, render-rate independence test.
- DEFERRED / unresolved: owner merges of PRs #18-#24; M3.4 (flag pinning, sim
  isolation gate, clang-cl); Vulkan-SDK-in-CI roll-out watch (winget pin 1.4.357.0).

## Next — FIRST action

1. Merge the stacked gap-close PRs bottom-up: #18 (w1) -> #19 (w2a) -> #20 (w2b) ->
   #21 (w3) -> #22 (w4) -> #23 (w5) -> #24 (w6), each after its checks pass, deleting
   branches as they land. Then extend the `main-gate` ruleset with the new
   RelWithDebInfo check.

## Queue

- From updated `main`, create the M3.4 slate: /fp:strict pinning for `eng_sim`, the
  float-grep lint in the pre-push path, game-binary self-check parity, clang-cl/UBSan
  golden (G11, G13).
- Phase 4 (assets) and Phase 5 (gameplay) may begin once M3.4 holds.
