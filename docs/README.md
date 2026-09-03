# docs/ — map

Which file answers which question. Start at the top; stop when the question is answered.

| Question | File | Shape / owner |
|---|---|---|
| What do I do first this session? | [next-session.md](next-session.md) | ADR-0025 wrap pin, ≤40 lines, replaced every wrap |
| What is actually built, and what is next? | [groundwork.md](groundwork.md) | Now / Not yet / Up next |
| Which tickets are open, ranked, with fences? | [WORK-FRONTLOG.md](WORK-FRONTLOG.md) | one row per claimable slice |
| How do vault theory and repo milestones connect? | [plans/README.md](plans/README.md) | bridge table + claim protocol |
| What exactly do I build for milestone X? | [plans/](plans/) | one plan per milestone: fence, gate, slice ledger |
| What is the long sequence (Phases 0–8)? | [ROADMAP.md](ROADMAP.md) | phase banners carry Now/Next/Later |
| Why is the engine shaped this way? | [ARCHITECTURE.md](ARCHITECTURE.md), [decisions/](decisions/README.md) | ADRs are immutable; supersede, do not edit |
| What is the design contract for the playable slice? | [slate-moba-proto-design.md](slate-moba-proto-design.md) | schemas, 11-step tick, slice DAG, acceptance tests |
| What evidence closed milestone X? | `slate-moba-<phase>-<id>.md` | one per milestone, ledger + evidence |
| What happened, narratively? | [JOURNAL.md](JOURNAL.md) | newest first, one entry per session |
| Provenance of a lane, and what it did not authorize? | [receipts/](receipts/) | lane receipts |
| What did a machine-executable lane plan look like? | `arc-*-manifest.json` | ARC manifests (schema 1.0) |
| Where did the fat pins go? | [session-archive/](session-archive/) | extracted pins, dated |
| Older per-milestone retrospectives? | [sessions/](sessions/) | **retired 2026-09-02**; slates + session-archive replace it |
| What must propagate elsewhere after a landing? | [custodian-queue.md](custodian-queue.md) | markers for `/custodian` |
| Which GPU/driver was the smoke run on? | [testing-hardware.md](testing-hardware.md) | hardware gate record |
| Historic gap and security ledgers | [gap-analysis-report.md](gap-analysis-report.md), [gap-close-ledger.md](gap-close-ledger.md), [security-consolidation-ledger.md](security-consolidation-ledger.md) | closed; reference only |

Fleet-level discovery is GromCodebase [FLEET-INDEX.md](https://github.com/shapeslines/GromCodebase/blob/main/docs/fleet/FLEET-INDEX.md);
flagged work is its `docs/fleet/GAP-REGISTER.md`. Never fork either here.
