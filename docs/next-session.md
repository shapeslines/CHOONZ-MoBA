# CHOONZ-MoBA — next session

## State @ `9628e62` · 2026-11-16 · integration candidate
M4.1 is on main; the CI runner handoff #66 has landed. PR #65 is still in review at this
observation. Refresh GitHub heads, checks and mergeability before acting on this snapshot.

## Shipped
- M4.1 cooker and sandbox content target: #57 and #63 (`4f66af1`).
- CI runner handoff and local-CI repair: #66 (`8fc6d9b`).
- Fleet noticeboard routing: #73 (`9628e62`).

## Companion packet
- summary: [receipts/20260902-pm-baseline.md](receipts/20260902-pm-baseline.md)
- nuances: [groundwork.md](groundwork.md) · pickup: [plans/README.md](plans/README.md)
- logging: historical session evidence in [JOURNAL.md](JOURNAL.md).

## Signals
- #68–#72 are open integration candidates. Their earlier checks found conflicts; refresh
  mergeability against current main. #71 depends on #68/#69 and #72 on #71, but the inspected
  PR heads are not ancestors of the later heads. Reconcile source lineage and preserve each
  logic-hash contract.
- The [runner handoff](ci-runner-handoff.md) describes a historical billing interruption and
  proposed bypass. Use the live ruleset and exact-head required checks; no bypass is assumed.
- The 2026-09-02 custodian marker remains in [custodian-queue.md](custodian-queue.md); its
  downstream completion has not been independently rechecked here.

## Next — FIRST action
1. If #65 is still open, qualify its current head against current main and required checks. Once
   it lands, reconcile #68 first, then its dependent work, with fresh checks after each base moves.

## Queue
- Complete the #68–#72 integration sequence and re-pin the simulation oracle after each accepted bump.
- Plan the `game-content-bridge` after its content and simulation predecessors land.
- Keep the docs-surface lint green and retain the historical receipts.
