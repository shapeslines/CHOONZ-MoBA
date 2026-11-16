# CHOONZ-MoBA — next session

## State @ `51f479a` · 2026-11-16 · integration candidate
M4.1, the PM plan #65 and the M5.0 map sim half #68 are on main. PR #69 carries
M5.1 command intake; refresh GitHub heads and checks before acting on this snapshot.

## Shipped
- M4.1 cooker and sandbox content target: #57 and #63 (`4f66af1`).
- CI runner handoff and local-CI repair: #66 (`8fc6d9b`).
- Fleet noticeboard routing: #73 (`9628e62`).
- PM baseline and M5 plan: #65 (`5e69755`).
- M5.0 map sim half: #68 (`51f479a`).

## Companion packet
- summary: [receipts/20260902-pm-baseline.md](receipts/20260902-pm-baseline.md)
- nuances: [groundwork.md](groundwork.md) · pickup: [plans/README.md](plans/README.md)
- logging: historical session evidence in [JOURNAL.md](JOURNAL.md).

## Signals
- #69–#72 are integration candidates. Refresh mergeability against current main. #71 depends
  on #68/#69 and #72 on #71, but the inspected
  PR heads are not ancestors of the later heads. Reconcile source lineage and preserve each
  logic-hash contract.
- The [runner handoff](ci-runner-handoff.md) describes a historical billing interruption and
  proposed bypass. Use the live ruleset and exact-head required checks; no bypass is assumed.
- The 2026-09-02 custodian marker remains in [custodian-queue.md](custodian-queue.md); its
  downstream completion has not been independently rechecked here.

## Next — FIRST action
1. Qualify #69 against current main and its milestone gates if still open; then reconcile
   #70–#72 with fresh checks and the predecessor fixes after each base moves.

## Queue
- Complete the #69–#72 integration sequence and re-pin the simulation oracle after each accepted bump.
- Plan the `game-content-bridge` after its content and simulation predecessors land.
- Keep the docs-surface lint green and retain the historical receipts.
