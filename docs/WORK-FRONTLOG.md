# CHOONZ-MoBA — WORK-FRONTLOG

Granular tickets for this repo. Immediate handoff is `docs/next-session.md`. Fleet flagged work lives in GromCodebase `docs/fleet/GAP-REGISTER.md` — **point there**, never fork a second register.

Claim protocol: pick the highest-ranked `open` row whose gate is met, write your seat + branch in the
State cell, create the worktree branch named in the plan, and open the slate. Rows sharing the
`engine/sim/` fence are serial unless the plan proves file-level disjointness.

| Rank | Work | Evidence / fence | State |
|---:|---|---|---|
| 1 | **M5.0 `m5-map-navigation`** — integer `MapGrid`/`MapCell`/`LaneDef`, cell↔world, neighbor ordering, `.mapdesc` codec, goldens | [plans/m5.0-map-navigation.md](plans/m5.0-map-navigation.md); fence `engine/sim/{include/sim/map.h,src/map.cpp}`, `tests/sim/map_tests.cpp`, `tests/CMakeLists.txt` (add_test only); branch `lane/moba-m5.0-map/<yyyymmdd>` | **in review — PR #68** (slate `slate-moba-phase5-m5.0.md`; sim half complete) |
| 2 | **M5.1 `m5-command-replay`** — in progress on `lane/moba-m5.1-command/20260903` — `Command`/`CommandReject`/`SimEvent`, ordering key, duplicate/stale/capacity rejection, live/replay parity | [plans/m5.1-command-replay.md](plans/m5.1-command-replay.md); fence `engine/sim/{include/sim/command.h,src/command.cpp}`, `tests/sim/command_replay_tests.cpp`; disjoint from rank 1 | open — parallel-capable |
| 3 | **`content-typed-payloads`** — validated hero/action/map/economy `.mba` payloads, byte-identical recook | [plans/content-typed-payloads.md](plans/content-typed-payloads.md); fence `engine/assets/`, `engine/asset_parsers/`, `tools/cooker/`, `tests/assets/`; ADR-0015 successor ADR required | open — plan exists; claim after PR #68 merges |
| 4 | M4.2 PNG + own inflate (cooker-only) | ROADMAP M4.2; fence `tools/cooker/`, `engine/asset_parsers/` | open — pull forward only if M5.0 map authoring needs PNG |
| 5 | M4.3 glTF strict subset → SoA mesh + frustum culling (G27) | ROADMAP M4.3; fence `tools/cooker/`, `engine/render/`, `engine/game/` | open — after typed payloads |
| 0 | **CI on fleet runner** — hosted Actions billing-locked; PR #66 prepares `ci.yml` for the self-hosted Windows runner; runner manager must bake the toolchain (`CHOONZ-MOBA-WIN-TOOLCHAIN-1`, WID `wid-20260903-choonz-moba-f56aa7`); owner adds admin bypass on `main-gate` for local-green merges | [ci-runner-handoff.md](ci-runner-handoff.md) | open — runner manager + owner |
| 6 | GAP-011 closing receipt (in-tree `CHOONZ/MOBA-proto` clone is gone from disk) | [custodian-queue.md](custodian-queue.md); GromCodebase `docs/fleet/GAP-REGISTER.md` row GAP-011, `FLEET-MAP.md` rows 51/79 | open — mailbox request posted 2026-09-02; GromCodebase seat owns the edit |
| 7 | Vault `20 Projects/moba/moba.md` vs `game-design.md` duplication | vault record `05 Exchange/records/2026-09-02-moba-game-design-reconcile.md` | done 2026-09-02 — moba = engine record (area make, blocks game-design); game-design = area hub (depends_on moba, pixelart-assistant, godot) |
| 8 | Fix `moba` skill line "maps to UE5/C++" (contradicts its own frontmatter) | GromAgentKit `live/skills/moba/SKILL.md` | open — skills repo, not here |

## Done (recent)

| Work | Evidence |
|---|---|
| M4.1 close-out: `content` target + sandbox baked texture | PR #63 → `4f66af1`; [slate-moba-phase4-m4.1.md](slate-moba-phase4-m4.1.md) |
| ADR-0025 surfaces filled, CLAUDE.md shim, objects map, plans bridge | PR #65 (`lane/moba-pm-baseline/20260902`) |
| Rescue PR #64 closed as superseded | GitHub #64 comment, 2026-09-02 |

## Pointers

- Groundwork: [docs/groundwork.md](groundwork.md)
- Sequence: [docs/ROADMAP.md](ROADMAP.md) · Bridge: [docs/plans/README.md](plans/README.md)
- Fleet work-state index: [GAP-REGISTER.md](https://github.com/shapeslines/GromCodebase/blob/main/docs/fleet/GAP-REGISTER.md)
