# CHOONZ-MoBA — next session

## State @ `4f66af1` · 2026-09-02 · homebase/pm-baseline
M4.1 closed (PR #63 squash-merged). ADR-0025 surfaces filled, `CLAUDE.md` shim, objects map in
`AGENTS.md`, theory→repo bridge in `docs/plans/`, M5.0 + M5.1 plans and the M5.0 ARC manifest written.
Depth: standard.

## Shipped
- `4f66af1` M4.1: sandbox texture through CMake `content` target [PR #63]
- `lane/moba-pm-baseline/20260902` PM baseline + roadmap translation (docs-only) [PR #65]

## Companion packet
- changelog: inline/none
- summary: [receipts/20260902-pm-baseline.md](receipts/20260902-pm-baseline.md)
- nuances: [groundwork.md](groundwork.md) "Conventions settled 2026-09-02"
- logging: inline/none
- suggestions / pickup: [plans/README.md](plans/README.md)

## Signals
- **state/flags:** ROADMAP Phase 4 banner now "M4.2–M4.4 on demand"; Phase 5 open. `docs/sessions/` retired. `.advisor/` gitignored.
- **communicated:** mailbox posture/start/wrap on `choonz-moba`; GAP-011 request to GromCodebase seat.
- **raised for /custodian:** 1 marker → [custodian-queue.md](custodian-queue.md)
- **FOR /brain:** distill → brain/choonz-moba.md ← CHOONZ-MoBA@4f66af1: vault is `Y:\GromBrain`; plan-before-slate convention; M4.1 DoD = cooker + `content` target + runtime `asset_load`.
- **DEFERRED / unresolved:** owner merge of the PM baseline PR; Carson disposition of vault `moba`/`game-design` note overlap; PR #64 closed, not merged.

## Next — FIRST action
1. Claim `m5-map-navigation`: read [plans/m5.0-map-navigation.md](plans/m5.0-map-navigation.md), create worktree branch `lane/moba-m5.0-map/<yyyymmdd>` under `GITHUB-ROOT/_worktrees/`, open `slate-moba-phase5-m5.0.md`, run the Debug matrix before the first edit.

## Queue
- M5.1 `m5-command-replay` ([plans/m5.1-command-replay.md](plans/m5.1-command-replay.md)) — parallel only with disjoint files.
- Write `plans/content-typed-payloads.md` (gate met) before claiming it.
- Keep `docs-surface-lint.py --repo .` green on wrap; patch vault `moba.md` projection fields.
