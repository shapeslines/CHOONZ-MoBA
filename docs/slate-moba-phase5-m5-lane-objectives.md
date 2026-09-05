# Phase 5 M5.3 Slate - Lane Objectives, Waves, Economy, and Victory (`m5-lane-objectives`)

**Status:** in progress (S0 landed; S1-S6 open)

**Branch:** `lane/moba-m5-lane-objectives/20260904` (worktree
`GITHUB-ROOT/_worktrees/CHOONZ-MoBA-objectives`)

**Plan of record:** [plans/m5-lane-objectives.md](plans/m5-lane-objectives.md)

**Base:** integration branch = `origin/main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe`
(M4.1) + `lane/moba-m5.0-map` (PR #68) + `lane/moba-m5.1-command` (PR #69) +
`lane/moba-m5-hero-combat` (PR #71), at `a2c5e8d`. The PR opens against `main` and is stacked
behind #68 -> #69 -> #71; it becomes mergeable in that order.

## Goal

Close the proto-design section 2.2 vertical slice inside `sim_tick`: deterministic lane creep
waves, a minion PUSH/ATTACK/RETURN state machine on authored waypoints, tower and core
objectives that acquire and fire on cooldown, a generalized death verdict that carries a
killer, gold and XP ledgers driven by authored `SimEconomyRule` rows, and a core-destruction
win condition. Deterministic, hashed, **zero** new RNG draws, and exactly one reviewed
`SIM_LOGIC_HASH` bump.

This is the fifth and last slice of the minimum vertical path (section 6:
`m5-map-navigation -> m5-command-replay -> content-typed-payloads -> m5-hero-combat ->
m5-lane-objectives`).

## Design decisions recorded

- **Sim-owned POD mirror, again.** `eng_sim` cannot read the asset layer, so
  `engine/sim/include/sim/match.h` mirrors section 3.3's `ObjectiveDef` and `EconomyRule` and
  section 3.2's `TeamDef` field for field, the way `hero.h` mirrored `HeroDef` at M5.2. The
  `MatchDef` bytes to sim defs translation is an `eng_game` slice and is out of scope.
- **Zero-default is feature-absent.** `SimMatchDef` is a by-value 336-byte member of
  `SimWorld` with `team_count == 0` meaning "no match", and `SimWorldConfig` gains
  `team_capacity` / `minion_capacity` / `objective_capacity`, all zero in
  `sim_world_config_default()`. Every existing fixture and the oracle world stay
  byte-identical through S1-S3; only S4's hash block moves them.
- **No new event queue.** The M5.2 `SimEvent` envelope carries four new kinds
  (`OBJECTIVE_DAMAGED`, `OBJECTIVE_DESTROYED`, `ECONOMY`, `MATCH_OVER`) inside the existing
  16-byte payload cap, so `sizeof(SimEvent)` stays 32 and the queue, its phases, its
  canonicality predicate, and the reject-at-source policy are untouched. This is the payoff
  the M5.2 slate predicted when it chose one envelope over three typed queues.
- **`SIM_EVENT_DEATH` is not widened.** Its 8-byte payload already carries `source` at offset
  4, and `sim_event_make_death` already takes it; `combat.cpp` just passes `HANDLE_NULL`
  today. This slice **populates** it from `HealthPool.last_damage_source`. No payload change.
- **Offset 0 is always an `EntityId`.** Today's `sys_effects_resolve` reads
  `sim_event_payload_u32(&event, 0u)` for **every** kind before branching, so every new
  payload puts a `u32` `EntityId` at offset 0 (`HANDLE_NULL` for `MATCH_OVER`). The slice
  promotes that from an accident to a rule enforced by `sim_event_is_canonical`.
- **One pipeline, still.** Minions, towers, and objectives damage and take damage through
  `resolve_effect` and the existing `DamageEvent` path, so section 3.3's one-pipeline rule
  holds unchanged and `health.current`'s owner stays `combat.cpp`. `sys_death` reads health,
  appends events, and defers destruction; it never writes health.
- **The death walk moves out of `combat.cpp`.** M5.2 put a hero-only death verdict at the tail
  of `sys_effects_resolve`. It becomes `sys_death` in `objectives.cpp`, generalized over
  heroes, minions, and objectives, in fixed pool order and ordered-view order within each.
- **A destroyed objective is not destroyed.** It stays a hashed `DESTROYED` `ObjectivePool`
  row so its cell, ownership, and identity remain stable for the rest of the match. Only
  heroes and minions route through `sim_destroy_deferred`.
- **Kill credit without a new tick-local buffer.** `sys_death` and `sys_economy` run after
  consume (the cadence the hero death walk already has), so `sys_economy` cannot read this
  tick's `DEATH` events. It reads `world->pending_destroy` in ascending ordinal plus the
  objective state transition - both already-hashed authoritative state - and resolves the
  killer through `last_damage_source`.
- **Saturating ledgers.** Awards clamp at `INT32_MAX`. Signed overflow is UB and would trip
  the UBSan gate; a wrapped ledger is not a determinism-safe state.
- **Zero RNG draws (ADR-0013).** Every tie is broken by a total order over hashed state
  (squared distance, then ascending `EntityId`), so the oracle **stream** must not move at S4
  - only the canonical-hash surface does.
- **Waypoints only.** No flow field, no A\*. M5.1 navigation stays its own milestone and this
  slice must not pre-empt its hash surface.

## Fence nuances recorded against the plan

- `add_required` in `sim.cpp` rejects a zero-size request, so each of the three new
  zero-capacity pools must be **skipped** in `sim_world_memory_required` and `sim_init` the
  way the empty map is, not passed through with size 0.
- `HealthPool` gains `last_damage_source`, hashed **inside the existing Health block** (after
  `damage_cooldown` per row) because it is a Health field; splitting it into the new block
  would make the first-divergence report lie about where the state lives. Its `SimStateField`
  entry is still **appended at the end of the enum** - hash position is the contract, enum
  position is not.
- `health_pool_remove` must clear `last_damage_source` to `HANDLE_NULL`, or a recycled entity
  index inherits a stale killer.
- `check_sim_pipeline_owner.cmake` needs two new rules (`ledger`, `match_state`, both owned by
  `objectives.cpp`), the `health` rule's pattern extended to cover `last_damage_source`
  (owner unchanged: `combat.cpp`), and `objectives.cpp` added to `OWNERS_cooldown_set` for
  minion and tower cooldown arming. Every new rule carries a probe line, like the existing four.
- `sys_waves` must **pre-flight** an entire wave (entity headroom plus every pool's free room)
  before creating the first entity: a partially spawned wave is worse than no wave and would
  violate reject-at-source.
- ROADMAP M5.5 phrases the minion tie-break as "by `EntityId` then distance"; the operative
  order is **nearest-first with `EntityId` as the tie-break**, and the plan pins that reading.
- Section 3.3's `ObjectiveDef` carries no offensive fields, so tower range, magnitude, and
  cadence are constants in `match.h` (the `hero.h` basic-attack precedent), not new mirror
  fields.
- Every reserved and pad field in the new mirror is **explicit**, so no struct has implicit
  tail padding - the canonical hash must never read a byte the compiler chose. `SimMinionDef`
  needs an explicit `uint32_t reserved2` to reach 32 bytes cleanly.
- `assets/maps/lane_test.mapdesc`, `tests/sim/map_golden.inc`, and `tests/sim/map_tests.cpp`
  are **not** modified. The new `lane_slice.mapdesc` fixture is additive, generated by a
  second description in `map_golden.py`, so M5.0's golden keeps its meaning.

## Baseline evidence (S0) - LANDED

Run on the untouched integration worktree at `a2c5e8d`, via `vcvars64.bat` ->
`cmake --preset ci` (`/WX`) -> `cmake --build build-ci --config Debug` ->
`ctest --test-dir build-ci -C Debug --output-on-failure --no-tests=error`:

- **Debug CTest: 100% tests passed, 52/52** (28.94 s). Includes `sim_map` (M5.0),
  `sim_command` (M5.1), `sim_hero_combat` and `sim_pipeline_owner` (M5.2),
  `check_sim_boundary`, `check_sim_compiler_policy` (+selftest), `check_sim_binary_parity`,
  `check_sim_isolation_selftest`, `check_sim_ubsan_tripwire`, `sandbox_baked_content`, and the
  shader/CI contract tests.
- `build-ci\tests\Debug\sim_oracle_probe.exe --sim-self-check`:

  ```
  sim_oracle ticks=10000 commands=923 final=0xac06a80d7f71b503 stream=0x4209159b82890bcb logic=0x46e9e287878ba88c
  ```

  These are the **M5.2** values (the integration branch carries the M5.2 bump), not the M5.0
  `0xff4e1ca0c779455b` / `0x218da333e6834496` / `0xcef8548df2b2a518` that the M5.2 slate
  recorded as its own baseline.
- Replay byte size pin `134812` and mutation pin `tick=4321 field=position_x entity=7` hold at
  this commit.
- All fifteen S4 pin sites were re-verified by grep at `a2c5e8d` and every file:line in the
  plan's pin table matches the value the table claims.

Note for anyone re-running the baseline: `build-ci/run-debug.cmd` in this worktree is a copy
from the M5.2 hero worktree and still `cd`s to `_worktrees/CHOONZ-MoBA-hero`. The S0 run used a
corrected scratchpad copy pointed at `CHOONZ-MoBA-objectives`. The `.cmd` files are build-dir
scratch and are gitignored; they are not repaired here.

## Hash bump (S4, the one permitted change) - PLANNED

```
SIM_LOGIC_HASH  0x46e9e287878ba88c  ->  0x5b47e648953a63fc
= first 8 bytes (big-endian) of SHA-256 of
  "M5.0|ordered-cache|damage-event-v1|same-tick|explicit-schedule-v1|map-grid-v1|hero-combat-v1|lane-objectives-v1"
```

Recipe verified on this branch: the M5.0 string reproduces `0xcef8548df2b2a518` and the current
string reproduces `0x46e9e287878ba88c`, so the generator is the same one both predecessors
used. The new hash and diff blocks append **after** the M5.2 `SimEvent` write-phase block, and
the new `SimStateField` entries append after `SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD`, so
every existing first-divergence report keeps its meaning. `0x46e9e287878ba88c` becomes a new
rejected historical row in `replay_tests.cpp`.

Fifteen pin sites move; the plan carries the exact file:line table. The RNG stream
`0x4209159b82890bcb` is expected **unchanged** - if it moves, that is `rng-drift`: stop, do not
re-pin. The `docs/ROADMAP.md` and `docs/JOURNAL.md` occurrences are past-tense narrative and
are **not** pins.

## Slice ledger

- [x] **S0** Baseline recorded above; branch and pin sites verified at `a2c5e8d`; plan of
  record and this slate written.
- [ ] **S1** `team.{h,cpp}` and `match.{h,cpp}`: `SimTeamId`, `TeamPool`, the five POD defs
  plus every validator, `sim_install_match_def`, `sim_spawn_objective`; `MinionPool` /
  `ObjectivePool` / `SimLedger` / `SimMatchState` and `HealthPool.last_damage_source`; the
  three config capacities plus `sim_world_memory_required` plus `sim_init` wiring, all
  zero-capacity by default. Hash untouched, every existing test count and the oracle unchanged.
- [ ] **S2** `objectives.{h,cpp}`: `sys_waves` and `sys_minion_ai` (waypoint movement via
  `fix_isqrt64`, the PUSH/ATTACK/RETURN FSM, nearest-then-`EntityId` tie-break); the four new
  `SimEventKind` values and builders; `sys_cooldown_tick` extended over `MinionPool`.
- [ ] **S3** `sys_tower_ai`; `last_damage_source` and `OBJECTIVE_DAMAGED` in `combat.cpp`; the
  death walk moved out into the generalized `sys_death`; `sys_economy`; the team-aware intake
  seam; `sim_tick` on the full 17-step order.
- [ ] **S4** Hash and diff appended; `last_damage_source` folded into the Health block;
  `SimStateField` entries; `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump
  to `0x5b47e648953a63fc`; new oracle identical in Debug and Release; all fifteen pins moved.
- [ ] **S5** Acceptance: section 7.2 items 6 (waves reach and destroy the tower then the core;
  one `MATCH_OVER`; stable event order), 1 (identical runs, Debug and Release), 3 (a full queue
  fails the source op and never drops; no partial wave), 4 (an extra economy rule does not move
  the RNG stream), plus ledger saturation, kill credit, and the extended pipeline-owner lint.
- [ ] **S6** Gates: `/WX` Debug / RelWithDebInfo / Release, `debug-asan`
  (`vcvars64 -vcvars_ver=14.44`), `check-clang-cl-determinism.ps1 -RequireCompiler
  -RequireUbsan`, isolation lint, binary parity, mutation pin intact. No Vulkan smoke (no
  renderer change). ROADMAP M5.3/M5.4/M5.5 status lines, JOURNAL, `docs/next-session.md`, PR
  body.

## Slice evidence

### S0 - baseline, branch, and pins

Worktree `GITHUB-ROOT/_worktrees/CHOONZ-MoBA-objectives`, branch
`lane/moba-m5-lane-objectives/20260904`, HEAD `a2c5e8d`
(`docs: M5.2 S6 gates and slate close (m5-hero-combat)`), `git status --porcelain` empty
before and after the S0 writes except the two documents this commit adds.

| Evidence | Value |
|---|---|
| Debug CTest | `100% tests passed out of 52`, 28.94 s |
| Oracle (Debug) | `ticks=10000 commands=923 final=0xac06a80d7f71b503 stream=0x4209159b82890bcb logic=0x46e9e287878ba88c` |
| Replay byte size | `134812` (pinned at `sim_determinism_tests.cpp:67`) |
| Mutation proof | `tick=4321 field=position_x entity=7` |
| Hash recipe check | M5.0 string -> `0xcef8548df2b2a518`; current string -> `0x46e9e287878ba88c` |
| Planned S4 key | `0x5b47e648953a63fc` |

Pin sites verified present at the claimed file:line: `replay.h:21`,
`sim_determinism_tests.cpp:25,67,68,115`, `hero_combat_tests.cpp:937`,
`tests/CMakeLists.txt:148,222`, `check_sim_binary_parity.cmake:40,41,42`,
`check-clang-cl-determinism.ps1:132`, `PULL_REQUEST_TEMPLATE.md:9`, `README.md:26`, plus the
`replay_tests.cpp:121,122,123` historical rows.
