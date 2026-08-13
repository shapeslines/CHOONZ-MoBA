# MOBA-proto — next session

## State @ implementation `133da41` · 2026-08-12 · DESKTOP-BK4F0OA/Codex

Phase 3 M3.0–M3.2 are complete. PR #14 (determinism/replay) and PR #15 (ECS) are merged.
PR #17 (systems/schedule) is ready, mergeable, independently accepted, and green on GitHub;
owner-approved merge is the only immediate gate. M3.3 has not started.

## Shipped

- `9c14fdfe` M3.0 determinism harness, canonical hash/diff, replay v1, and replay CLI — PR #14 merged.
- `7ce2b45b` M3.1 arena entity manager, sparse-set SoA pools, and deferred destruction — PR #15 merged.
- `133da41` M3.2 ordered views, phase-buffered damage events, and explicit same-tick schedule — PR #17 ready.

## Verified

- M3.2 `/WX` Debug and Release builds pass; 25/25 CTest entries pass in each configuration.
- The two independent 10,000-tick runs end at `0x637628abff59c823` in Debug and Release.
- Controlled mutation reports exactly `tick=4321 field=position_x entity=7`.
- Replay remains format v1 with 16-byte commands and logic hash `0xab96814425ba80a4`.
- The 17-file sim boundary and direct `eng_sim → core + math + serialize` dependency are clean.
- Independent acceptance and final GitHub MSVC/CodeQL checks passed on the implementation head.

## Signals

- **state/flags:** damage resolves in the same tick; the two-phase event queue deliberately preserves
  a future next-tick experiment by moving only publication. Any timing-policy change requires a new
  reviewed logic hash. Replay container version stays 1 unless its layout changes.
- **communicated:** PR #17 carries the complete review surface; independent acceptance passed twice
  across whitespace/docs-only successors. The full cold-resume baton is mirrored to the vault.
- **raised for /custodian:** none; architecture, roadmap, journal, slate, and restart state are current.
- **FOR /brain:** distill → `brain/moba-deterministic-sim-spine.md` ←
  `MOBA-proto@133da41`: M3.0–M3.2 oracle, semantic-hash exclusions, ordered-system rule, and event-timing seam.
- **DEFERRED / unresolved:** owner merge of PR #17; M3.3 platform accumulator/presentation snapshots;
  M3.4 build isolation; hosted Vulkan SDK installation before making Vulkan required in CI.

## Next — FIRST action

1. Owner-review and merge PR #17. Confirm its head and required checks remain green; do not begin
   M3.3 from the open M3.2 branch.

## Queue

- From updated `main`, create `moba/slate-phase3-presentation` and execute
  [`M3.3 presentation slate`](slate-moba-phase3-m3.3.md).
- Preserve the M3.2 command stream, per-tick hash oracle, and tick-4321 diagnostic unchanged.
- Keep the platform accumulator outside `eng_sim`; fixed→float conversion belongs only to game/present glue.
- Measure same-tick versus next-tick damage before changing policy; define stale-handle behavior first.
- Close M3.4 separately before opening Phase 4/5 work.
