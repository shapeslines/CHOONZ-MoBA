# CHOONZ-MoBA — next session

## State @ `4f66af1` · 2026-09-02 · homebase/pm-baseline
Main still at `4f66af1` (M4.1). Six lanes await owner merge: #65 docs, #66 CI runner handoff, #68 M5.0
map grid, #69 M5.1 command intake, #70 typed content payloads, #71 M5.2 hero combat. Depth: standard.

## Shipped
- `4f66af1` M4.1: sandbox texture through CMake `content` target [PR #63]
- `lane/moba-pm-baseline/20260902` PM baseline + roadmap translation (docs-only) [PR #65]
- `lane/moba-ci-runner/20260902` CI → fleet runner handoff + local-ci fix [PR #66]
- `lane/moba-m5.0-map/20260903` M5.0 map grid + `.mapdesc` + canonical hash [PR #68, supersedes #67]
- `lane/moba-m5.1-command/20260903` M5.1 command intake + live/replay parity [PR #69]
- `lane/moba-content-payloads/20260903` typed content record payloads, ADR-0016 [PR #70]
- `lane/moba-m5-hero-combat/20260903` M5.2 hero combat, unified effect pipeline, stacked on #68/#69 [PR #71]

## Companion packet
- changelog: inline/none
- summary: [receipts/20260902-pm-baseline.md](receipts/20260902-pm-baseline.md)
- nuances: [groundwork.md](groundwork.md) "Conventions settled 2026-09-02"
- logging: inline/none
- suggestions / pickup: [plans/README.md](plans/README.md)

## Signals
- **state/flags:** PR #68 bumps `SIM_LOGIC_HASH` → `0xcef8548df2b2a518`; PR #71 bumps it again → `0x46e9e287878ba88c`, oracle → `0xac06a80d7f71b503`/`0x4209159b82890bcb`; `AGENTS.md` §1 must be refreshed on merge. Phase 5 open; `docs/sessions/` retired.
- **communicated:** mailbox posture/start/wrap on `choonz-moba`; GAP-011 request to GromCodebase seat.
- **raised for /custodian:** 1 marker → [custodian-queue.md](custodian-queue.md)
- **FOR /brain:** distill → brain/choonz-moba.md ← CHOONZ-MoBA@4f66af1: vault is `Y:\GromBrain`; plan-before-slate convention; M4.1 DoD = cooker + `content` target + runtime `asset_load`.
- **DEFERRED / unresolved:** Actions billing-locked; owner adds admin bypass on `main-gate` and merges #65 → #66 → #68 → #69 → #70 → #71 on local green (`docs/ci-runner-handoff.md`); runner manager provisions the toolchain (WID `wid-20260903-choonz-moba-f56aa7`). ASan needs `vcvars64 -vcvars_ver=14.44` (M5.0 slate).

## Next — FIRST action
1. Owner merges #65 → #66 → #68 → #69 → #70 → #71 (local green + admin bypass). Then write `plans/m5-lane-objectives.md` from the merged `main` (minions, towers, objectives on the M5.0 lanes; hero pipeline in tree) and claim it; refresh `AGENTS.md` §1 oracle values in that lane.

## Queue
- Presentation slice for the M5.0 heightfield mesh (deferred from ROADMAP M5.0).
- Game-layer `HeroDef` bytes → `SimHeroDef` translation (`eng_game`), named in the hero slate.
- Keep `docs-surface-lint.py --repo .` green on wrap; patch vault `moba.md` projection fields.
