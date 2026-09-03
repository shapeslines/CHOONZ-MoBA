# docs/plans — the theory → repo bridge

The MOBA's theoretical roadmap lives in three places that do not name repo files: the vault decision
index (`$OIP_VAULT/20 Projects/moba/moba_engine_decision_index.json`, decisions `D-xx` grouped into
phases P0 Foundation → P6 Harden/scale), the vault planning slices (`slice-m4-*.md`, `slice-m5-*.md`,
`slice-m6-7-*.md`, all "unfenced planning seeds"), and the repo's ratified design contract
(`docs/slate-moba-proto-design.md` §6, the seven-slice DAG). `docs/ROADMAP.md` holds the long
milestone sequence. This folder is where each of those becomes one **claimable, fenced plan**.

## The bridge

| Decision-index phase | ROADMAP milestone | Proto-design slice | Vault seed | Repo plan | Write fence | Gate | Status |
|---|---|---|---|---|---|---|---|
| P3 Rendering / P4 tools | M4.0 direct loaders + registry | — | `slice-m4-asset-runtime-boundary` | (landed) | `engine/assets/` | — | ✅ `f30dbb7` (#52) |
| P4 Gameplay + tools | M4.1 cooker + `.mba` v1 | prerequisite of `content-typed-payloads` | `slice-m4-offline-asset-cooker` | [m4.1-closeout.md](m4.1-closeout.md) | `engine/asset_parsers/`, `tools/cooker/` | — | ✅ `6571ee4` (#57) + `4f66af1` (#63) |
| P1 Deterministic sim core | M5.0 map grid | `m5-map-navigation` | `slice-m5-deterministic-map-grid` | [m5.0-map-navigation.md](m5.0-map-navigation.md) + `../arc-m5.0-map-navigation-manifest.json` | `engine/sim/` map files, `tests/sim/map_tests.cpp` | M4.1 accepted ✅ | **open — claim next** |
| P1 / P2 Netcode spine | M5.2 command system, M6.0 reject reasons | `m5-command-replay` | `slice-m5-input-quantization` (partial) | [m5.1-command-replay.md](m5.1-command-replay.md) | `engine/sim/` command files, `tests/sim/command_replay_tests.cpp` | M3.4 replay/hash ✅ | in progress — `lane/moba-m5.1-command/20260903` |
| P4 / P5 Content scale | M4.1 successor (typed payloads) | `content-typed-payloads` | `slice-m4-offline-asset-cooker` | [content-typed-payloads.md](content-typed-payloads.md) | `engine/assets/`, `engine/asset_parsers/`, `tools/cooker/`, `tests/assets/` | M4.1 ✅, M5.0 map bytes (PR #68) | open — claim after #68 merges |
| P4 Gameplay | M5.3 abilities, M5.4 combat | `m5-hero-combat` | — | (after map, command, payload plans) | `engine/sim/` combat files | three slices above | later |
| P4 Gameplay | M5.5 minion/tower AI | `m5-lane-objectives` | — | (after hero-combat) | `engine/sim/` objective files | hero-combat | later |
| P2 Netcode spine | M6.1–M6.7 | `m6-authority-replication` | `slice-m6-7-server-client-parity` | (after lane-objectives) | `engine/net/` (new), `tests/net/` | first playable local slice | later |
| P3 Rendering | M7.x vertical slice | `m7-presentation` | — | (after replication contracts) | `engine/game/`, `engine/render/`, `tests/game/` | snapshot + command contracts | later |
| P4 tools | M4.2 PNG/inflate, M4.3 glTF + culling, M4.4 hot reload | — | — | pull forward on demand | `tools/cooker/`, `engine/render/` | as needed by Phase 5 | parked |

Minimum vertical path (proto-design §6): `m5-map-navigation → m5-command-replay →
content-typed-payloads → m5-hero-combat → m5-lane-objectives`. The first playable local slice closes
after the fifth.

## What a plan file contains

1. **Goal** — one paragraph, observable.
2. **Spec sources** — the exact sections of the proto-design, ROADMAP, ADRs, and vault seeds it
   implements. The plan restates the contract; it never re-derives it.
3. **Write fence** — exact repo paths this lane may create or edit. Anything else is a fence breach.
4. **Out of scope** — what the lane must not touch even if convenient.
5. **Slice ledger** — ordered checkboxes; each slice ends green and committed.
6. **Acceptance** — which proto-design §7.2 tests it adds, plus the invariants it must keep (oracle,
   logic hash, parity, ASan/UBSan, Vulkan smoke where applicable).
7. **Held questions** — open design values with the default assumption the lane proceeds under.
8. **Branch / worktree / slate** — names, so two seats cannot collide.

## Claim protocol

1. Pick the highest-ranked open row in `../WORK-FRONTLOG.md` whose gate is met.
2. Write your seat and branch in that row's State cell; post the mailbox `start` frame with every
   fenced path as `--ref repo:choonz-moba/<path>`.
3. Create the worktree under `GITHUB-ROOT/_worktrees/` on `lane/moba-<id>/<yyyymmdd>` from green `main`.
4. Copy the plan's slice ledger into a new `../slate-moba-<phase>-<id>.md`; record baseline evidence
   (Debug matrix, oracle) before the first edit.
5. Build slice by slice; PR against `main`; the slate carries the evidence; the ROADMAP status flips
   on merge; the vault note's projection fields are patched at wrap.

## Theory that is locked but not yet built

The decision index locks several P4 choices that no repo milestone has claimed yet: the Lua 5.4 no-JIT
ability VM (`D-10`, `D-16`), the job system with declared R/W sets (`D-09`), Q32.32 as the eventual
numeric substrate (`D-15`; the repo is Q16.16 per ADR-0002 and the reconciliation register
`moba_engine_reconciliation.llm.md` records the divergence). Design against the locked choice; never
describe it as shipped. A plan that needs one of these must first add an ADR or a reconciliation row.
