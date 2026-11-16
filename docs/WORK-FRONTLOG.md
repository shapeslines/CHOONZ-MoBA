# CHOONZ-MoBA — WORK-FRONTLOG

Granular tickets for this repo. Immediate handoff is `docs/next-session.md`. Fleet flagged work lives in GromCodebase `docs/fleet/GAP-REGISTER.md` — **point there**, never fork a second register.

Claim protocol: pick the highest-ranked `open` row whose gate is met, write your seat + branch in the
State cell, create the worktree branch named in the plan, and open the slate. Rows sharing the
`engine/sim/` fence are serial unless the plan proves file-level disjointness.

| Rank | Work | Evidence / fence | State |
|---:|---|---|---|
| 1 | **M5.0 `m5-map-navigation`** — integer `MapGrid`/`MapCell`/`LaneDef`, cell↔world, neighbor ordering, `.mapdesc` codec, goldens | [plans/m5.0-map-navigation.md](plans/m5.0-map-navigation.md); fence `engine/sim/{include/sim/map.h,src/map.cpp}`, `tests/sim/map_tests.cpp`, `tests/CMakeLists.txt` (add_test only); branch `lane/moba-m5.0-map/<yyyymmdd>` | **in review — PR #68** (slate `slate-moba-phase5-m5.0.md`; sim half complete) |
| 2 | **M5.1 `m5-command-replay`** — `Command`/`CommandReject`/`SimEvent`, ordering key, duplicate/stale/capacity rejection, live/replay parity | [plans/m5.1-command-replay.md](plans/m5.1-command-replay.md); planned test is `tests/sim/command_replay_tests.cpp`, while PR #69 carries `tests/sim/command_tests.cpp`; reconcile the fence | **in review — PR #69** (`slate-moba-phase5-m5.1.md`) |
| 3 | **`content-typed-payloads`** — validated hero/action/map/economy `.mba` payloads, byte-identical recook | [plans/content-typed-payloads.md](plans/content-typed-payloads.md); fence `engine/assets/`, `engine/asset_parsers/`, `tools/cooker/`, `tests/assets/`; ADR-0015 successor ADR required | **in review — PR #70** (ADR-0016; `slate-moba-phase5-content-payloads.md`) |
| 3b | **M5.2 `m5-hero-combat`** — `SimHeroDef` mirror, hero/projectile/status pools, `SimEvent` envelope, one `resolve_effect` pipeline, ATTACK/CAST commands | [plan snapshot from PR #71](https://github.com/shapeslines/CHOONZ-MoBA/blob/bb7248cb11e618e43b2a479ccd35cd69733359ec/docs/plans/m5-hero-combat.md); fence `engine/sim/` hero/combat files; lint `sim_pipeline_owner` | **in review — PR #71** (depends on #68/#69; reconcile head ancestry; one logic-hash bump) |
| 4 | **ADR-0010 generated `asset_ids.gen.h` + build-time collision check** — cooker manifest mode; unpaid debt named in ADR-0016 | fence `tools/cooker/`, `cmake/`, `engine/asset_parsers/include/assets/`; ADR-0010 | open — small follow-on after the content lane merges |
| 4b | M4.2 PNG + own inflate (cooker-only) | ROADMAP M4.2; fence `tools/cooker/`, `engine/asset_parsers/` | open — pull forward only if M5.0 map authoring needs PNG |
| 3c | **M5.5 `m5-lane-objectives`** — `SimMatchDef`, team/minion/objective pools, waves, minion FSM, towers, death with killer, ledgers, match over | [plan snapshot from PR #72](https://github.com/shapeslines/CHOONZ-MoBA/blob/5b6164c8fa9878c2184ab9860399453be09f51d6/docs/plans/m5-lane-objectives.md); fence `engine/sim/` objective files; `lane_slice.mapdesc` | **in review — PR #72** (depends on #71; reconcile head ancestry; one logic-hash bump) |
| 5a | **`game-content-bridge`** — `.mba` hero/objective/economy records → `SimHeroDef`/`SimMatchDef` in `eng_game`; `content.h` static_assert bridge | plan to write from the merged `main`; fence `engine/game/`, `tests/game/` | open — next claim after #72 |
| 5 | M4.3 glTF strict subset → SoA mesh + frustum culling (G27) | ROADMAP M4.3; fence `tools/cooker/`, `engine/render/`, `engine/game/` | open — after typed payloads |
| 0 | **CI runner follow-through** — PR #66 landed at `8fc6d9b`; verify current runner enrollment and exact-head checks before relying on the historical handoff | [ci-runner-handoff.md](ci-runner-handoff.md); request `CHOONZ-MOBA-WIN-TOOLCHAIN-1` | open — live runner and admission evidence |
| 6 | GAP-011 closing receipt (in-tree `CHOONZ/MOBA-proto` clone is gone from disk) | [custodian-queue.md](custodian-queue.md); GromCodebase `docs/fleet/GAP-REGISTER.md` row GAP-011, `FLEET-MAP.md` rows 51/79 | open — mailbox request posted 2026-09-02; GromCodebase seat owns the edit |
| 7 | Vault `20 Projects/moba/moba.md` vs `game-design.md` duplication | vault record `05 Exchange/records/2026-09-02-moba-game-design-reconcile.md` | done 2026-09-02 — moba = engine record (area make, blocks game-design); game-design = area hub (depends_on moba, pixelart-assistant, godot) |
| 8 | Fix `moba` skill line "maps to UE5/C++" (contradicts its own frontmatter) | GromAgentKit `live/skills/moba/SKILL.md` | open — skills repo, not here |

## Done (recent)

| Work | Evidence |
|---|---|
| M4.1 close-out: `content` target + sandbox baked texture | PR #63 → `4f66af1`; [slate-moba-phase4-m4.1.md](slate-moba-phase4-m4.1.md) |
| Rescue PR #64 closed as superseded | GitHub #64 comment, 2026-09-02 |

The ADR-0025 surfaces, `CLAUDE.md` shim, objects map and plans bridge are carried in PR #65;
check its live GitHub state for landing.

## Pointers

- Groundwork: [docs/groundwork.md](groundwork.md)
- Sequence: [docs/ROADMAP.md](ROADMAP.md) · Bridge: [docs/plans/README.md](plans/README.md)
- Fleet work-state index: [GAP-REGISTER.md](https://github.com/shapeslines/GromCodebase/blob/main/docs/fleet/GAP-REGISTER.md)
