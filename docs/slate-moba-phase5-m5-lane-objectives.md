# Phase 5 M5.3 Slate - Lane Objectives, Waves, Economy, and Victory (`m5-lane-objectives`)

**Status:** complete - S0-S6 landed, PR open, owner merge gated (hosted Actions is
billing-locked; see `docs/ci-runner-handoff.md` for the interim local-green merge path).

**ROADMAP milestone:** M5.5 (minion/tower AI). The plan id and this slate's title carry
`M5.3` from the draft numbering; the milestone this lane closes is **M5.5**, and it also
closes M5.4's kill-credit and gold/XP-on-death items and the proto-design section 2.2
vertical slice.

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
  (squared distance, then ascending `EntityId`), so the tick stream is untouched: `world->rng`
  after N ticks stays a function of `(seed, N)` alone. (This bullet originally added "so the
  oracle **stream** must not move at S4". That was wrong - the oracle `stream=` field is a
  digest over the canonical state hash, not an RNG trace, and it moves with every hash bump.
  See the Hash bump correction below.)
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

## Hash bump (S4, the one permitted change) - LANDED

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

Fifteen pin sites move; the plan carries the exact file:line table.

**Correction (S4/S5).** This section originally expected the oracle `stream=` field to hold at
`0x4209159b82890bcb`. It does not, and it must not: `engine/sim/src/oracle.cpp:44` folds every
per-tick `sim_hash_state` into `hash_stream_digest`, so `stream` is a digest **over** the
canonical state hash and moves with every reviewed hash bump - as it did at M5.0 and again at
M5.2. It is re-pinned to `0xb6067f3f0955b292`. The real `rng-drift` signal is that
`mm::pcg32_next` has exactly one call site in the whole engine (`sys_rng_advance`,
unconditional, once per tick), so `world->rng` after N ticks is a function of `(seed, N)` alone
and is independent of any lane-objectives configuration; S5 asserts that directly.

The `docs/ROADMAP.md` and `docs/JOURNAL.md` occurrences are past-tense narrative and are **not**
pins.

## Decisions recorded during execution (deviations from the plan of record)

Each of these is a deliberate departure from the plan as written at S0, taken during S1-S5 and
recorded here so the plan and the code do not silently disagree. The plan of record has been
corrected in place for the two that were wrong in the plan rather than merely unstated.

- **`move_speed` is a velocity, not a per-tick displacement.** The plan's mirror comment said
  "Q16.16 world units per tick". `sys_minion_ai` writes `move_speed` straight into the Velocity
  row and `sys_movement` is the sole integrator, so the per-tick step is
  `fix_mul(move_speed, SIM_DT_FIXED)`. At 30 Hz `SIM_DT_FIXED` is 2184, so
  `fix_from_int(15)` steps **32760**, not 32768: a creep never lands exactly on a lane cell
  centre. Consequence for authors, learned the hard way in S5: a creep `attack_range` authored
  as *exactly* the perpendicular distance to an objective cell misses forever. Author with
  slack. **Plan corrected.** (`match.h`'s own field comment still reads "per tick"; it is
  outside this slice's fence and is queued for the M5.6 pointer sweep.)
- **Acquire and fire on the same tick.** A creep that has just come into range does not stand
  idle for a tick before swinging: `sys_minion_ai` sets `state = ATTACK`, sets the target, and
  routes the first `SIM_EFFECT_PROJECTILE_DAMAGE` through `resolve_effect` in one step. This is
  the `sys_hero_actions` cadence, and it keeps the FSM's observable states to three.
- **`sim_spawn_match_objectives` was added.** The plan made spawning the objective entities the
  caller's job through `sim_spawn_objective` alone. S1 added the bounded convenience that
  instantiates the whole installed match - for each team in ascending team id, its tower then
  its core, at the cells its `SimTeamDef` names - because every caller wanted exactly that
  order and letting each one re-derive it would have made spawn order a caller property rather
  than a match-def property. A world with no match configured succeeds and creates nothing.
- **`sys_economy` scans the write phase, not a tick-local buffer.** `sys_death` and
  `sys_economy` both run *after* `sim_event_queue_consume`, so `sys_economy` cannot read this
  tick's `DEATH` events through `sim_event_queue_read`. It reads `world->pending_destroy` in
  ascending ordinal, plus the `OBJECTIVE_DESTROYED` rows `sys_death` just appended to the write
  phase (bound snapshotted before the walk, so the `ECONOMY` rows it appends can never
  re-enter). Both inputs are already-hashed authoritative state, so no new tick-local buffer
  and no new hashed field were needed.
- **Intake honours `world->match_state.over` directly.** The plan had the driver wire
  `CommandIntakeConfig.match_over` from the world. `sim_validate_command` now rejects on
  `config.match_over || world->match_state.over`, so a driver that forgets to wire the flag
  still cannot feed intent into a decided match. The config field is retained for a server that
  knows the match is over before the sim does.
- **The oracle `stream=` field is not an RNG stream, and it moved at S4 as it must.** The plan
  listed a moving `stream` under `rng-drift` and said "stop, do not re-pin". That was wrong.
  `engine/sim/src/oracle.cpp:44` folds **every per-tick `sim_hash_state`** into
  `hash_stream_digest`, so `stream` is a digest *over* the canonical state hash and moves with
  every reviewed hash bump - as it did at M5.0 and again at M5.2. The real `rng-drift` signal is
  that `mm::pcg32_next` has **exactly one call site** in the whole engine (`sys_rng_advance`,
  unconditional, once per tick, ADR-0007/ADR-0013), so `world->rng` after N ticks is a function
  of `(seed, N)` alone and is independent of any lane-objectives configuration. `rng-drift`
  means a second call site appears, or two worlds sharing a seed disagree on `world->rng` after
  N ticks. **Plan corrected**, and the S5 acceptance test asserts the rule against `world->rng`
  directly rather than against the oracle line.
- **Tower tier 1 was inverted, and is fixed (S5).** S3 implemented tier 1 as "an enemy hero
  whose own `last_damage_source` names a hero on the tower's team" - which is the *ally hit the
  enemy* relation, the inverse of what ROADMAP M5.5 asks for. The ROADMAP reads "enemy attacking
  allied hero", so the predicate must be read off the **ally's** kill-credit slot: a candidate
  `E` qualifies when some **live** hero `H` on the tower's team carries
  `health(H).last_damage_source == E`. `E` need not itself be a hero - a creep beating on our
  carry is exactly what a tower is meant to punish. The allied-hero scan is
  `component_pool_ordered_view` over `HeroPool` and decides membership only, never order, so the
  nearest-then-ascending-`EntityId` rule remains the total order. Fixed in
  `engine/sim/src/objectives.cpp` (`attacks_allied_hero`, `TARGET_TIER_ATTACKS_ALLIED_HERO`),
  the plan's wording corrected, and the S3 test rewritten to pin the ROADMAP direction **and**
  to assert that the inverted trigger no longer fires. The fix moves no pin: it changes nothing
  in a world with zero hero, team, or objective capacity, and the oracle world has all three at
  zero.

## Slice ledger

- [x] **S0** Baseline recorded above; branch and pin sites verified at `a2c5e8d`; plan of
  record and this slate written.
- [x] **S1** `team.{h,cpp}` and `match.{h,cpp}`: `SimTeamId`, `TeamPool`, the five POD defs
  plus every validator, `sim_install_match_def`, `sim_spawn_objective`; `MinionPool` /
  `ObjectivePool` / `SimLedger` / `SimMatchState` and `HealthPool.last_damage_source`; the
  three config capacities plus `sim_world_memory_required` plus `sim_init` wiring, all
  zero-capacity by default. Hash untouched, every existing test count and the oracle unchanged.
- [x] **S2** `objectives.{h,cpp}`: `sys_waves` and `sys_minion_ai` (waypoint movement via
  `fix_isqrt64`, the PUSH/ATTACK/RETURN FSM, nearest-then-`EntityId` tie-break); the four new
  `SimEventKind` values and builders; `sys_cooldown_tick` extended over `MinionPool`.
- [x] **S3** `sys_tower_ai`; `last_damage_source` and `OBJECTIVE_DAMAGED` in `combat.cpp`; the
  death walk moved out into the generalized `sys_death`; `sys_economy`; the team-aware intake
  seam; `sim_tick` on the full 17-step order.
- [x] **S4** Hash and diff appended; `last_damage_source` folded into the Health block;
  `SimStateField` entries; `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump
  to `0x5b47e648953a63fc`; new oracle identical in Debug and Release; all fifteen pins moved.
- [x] **S5** Acceptance: section 7.2 items 6 (waves reach and destroy the tower then the core;
  one `MATCH_OVER`; stable event order), 1 (identical runs, Debug and Release), 3 (a full queue
  fails the source op and never drops; no partial wave), 4 (an extra economy rule does not move
  the RNG stream), plus ledger saturation, kill credit, and the extended pipeline-owner lint.
- [x] **S6** Gates: `/WX` Debug / RelWithDebInfo / Release, `debug-asan`
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

### S1-S4 - the sim core (Agent B)

| Slice | Commit | Evidence |
|---|---|---|
| S1 | `a01e013` | `team.{h,cpp}`, `match.{h,cpp}`, `objectives.{h,cpp}` foundations; `MinionPool` / `ObjectivePool` / `SimLedger` / `SimMatchState`; `HealthPool.last_damage_source`; three zero-default capacities. Hash untouched, oracle unchanged. |
| S2 | `bad2eb2` | `sys_waves` + `sys_minion_ai` (waypoints via `fix_isqrt64`, PUSH/ATTACK/RETURN, nearest-then-`EntityId`); four new `SimEventKind` values and builders; `sys_cooldown_tick` over `MinionPool`; `assets/maps/lane_slice.mapdesc` + `lane_slice_golden.inc` from a second `map_golden.py` description. Oracle unchanged. |
| S3 | `234e877` | `sys_tower_ai`; `last_damage_source` and `OBJECTIVE_DAMAGED` in `combat.cpp`; the death walk moved out into the generalized `sys_death`; `sys_economy`; the team-aware intake seam. 20 cases, 661 checks; Debug ctest 53/53; oracle unchanged. |
| S4 | `c8c8a24` | Hash and diff blocks appended after the M5.2 `SimEvent` block; `last_damage_source` folded into the Health block; `SimStateField` appended; `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump; all fifteen pins moved. 23 cases, 722 checks; Debug and Release ctest 53/53. |

### S5 - acceptance (proto-design section 7.2)

One commit, `tests/sim/lane_objectives_tests.cpp` plus the tier-1 fix in
`engine/sim/src/objectives.cpp`. The acceptance match decodes
`assets/maps/lane_slice.mapdesc` from the independent golden `.inc` rather than authoring the
grid in C++, so it runs on exactly the bytes the repo ships.

**The match is deliberately asymmetric.** A mirror match on a mirror map is a stalemate: equal
waves meet at the midpoint and annihilate each other forever, and no core ever falls. Exactly
one authored field breaks the mirror - team 1's tower carries `SIM_TARGET_POLICY_NONE`, so
team 0's tower is the only objective that shoots - and team 1's objectives are authored thin.
The asymmetry is therefore **data**, never a special case in the sim.

| Item | Test | What it proves |
|---|---|---|
| 6 | `the_scripted_match_takes_the_tower_then_the_core_and_ends_once` | Waves flow on the authored lane clock, minions reach the enemy tower, the tower falls, then the core; `match_state.over/winner/end_tick` are set; `OBJECTIVE_DAMAGED`, `DEATH`, `ECONOMY` all fire and `MATCH_OVER` fires **exactly once**, after the core's `OBJECTIVE_DESTROYED` on the same tick. Both losing objectives survive as hashed `DESTROYED` rows (never destroy-deferred); both winning objectives are untouched at full health. Two fresh runs agree on the whole ordered `(kind, tick, append_ordinal, payload)` stream, on every per-kind count, on the milestone list, and on `sim_hash_state` (`sim_diff_state` reports `SIM_STATE_FIELD_NONE`). After the verdict, 20 further ticks still succeed and still hash identically on both runs, and intake rejects an otherwise-authorized command with `COMMAND_REJECT_MATCH_OVER`. |
| 1 | `the_recorded_match_replays_to_the_same_hashes` | 3,000 ticks of the same scripted match recorded through the replay v1 codec (header + per-tick record; 4 real scripted commands, so the stream is non-trivial), then played back into a fresh world from the bytes alone: `sim_hash_state` agrees at all 6 500-tick checkpoints and at the end, the event digest agrees, the byte size matches `REPLAY_HEADER_ENCODED_SIZE + 3000 * 12 + 4 * 16`, and `replay_require_end` is clean. |
| 3 | `a_full_event_queue_fails_the_death_and_the_award_whole` | With the write phase filled to the brim, `sys_death` fails the **source operation**: nothing is appended, nothing is dropped, `pending_destroy` is untouched, and `sim_hash_state` is byte-identical before and after. Draining lets the identical call through. Then with room for one of the two matching `MINION_KILL` rules, `sys_economy` fails whole - no partial award, ledgers still zero, hash unchanged - and draining lets it pay both. (The wave half of item 3 is `a_wave_that_does_not_fit_fails_the_source_operation`, from S2.) |
| 4 | `an_extra_economy_rule_moves_the_ledger_and_never_the_rng` | Two worlds differing only by one extra `SimEconomyRule` run 3,000 ticks each: the ledgers **differ** (`xp[0]` by exactly 80, two objectives at 40 each), the state hashes and event digests differ, and `world->rng.state` / `world->rng.inc` are **identical** and equal to the seed advanced exactly 3,000 times. A third world sharing the lean config reproduces its final hash, its event digest, and all 6 checkpoints. |
| - | `a_live_ledger_saturates_and_the_clamped_world_still_hashes` | A ledger seeded near `INT32_MAX` takes real in-match awards, clamps at `INT32_MAX` both times, never wraps negative, and the clamped world is still canonical (`sim_hash_state != 0`). |
| - | `tower_priority_is_attacker_of_an_ally_then_nearest_minion_then_hero` | The corrected tier 1 in both directions: the inverted trigger no longer fires; an ally's kill-credit slot naming an enemy hero does; a dead ally empties the tier; a **non-hero** attacker (a far minion) wins tier 1 over the nearer tier-2 minion; an attacker that is no longer live is treated as absent, never as a tick failure. |
| - | `ownership_is_by_team_and_a_decided_match_rejects_every_command` | Team ownership through the real intake: player `p` owns team `p`, a wrong-team actor is `COMMAND_REJECT_UNAUTHORIZED_ACTOR`, and a decided match is `COMMAND_REJECT_MATCH_OVER` whoever sends it. |

Suite result: **28 cases, 1109 checks, 0 failed** (`engine_tests --suite sim_lane_objectives`),
up from 23 cases / 722 checks at S4.

### S6 - gates

Every configuration reports the identical oracle line, unchanged from S4 - the tier-1 fix moves
no pin, because it changes nothing in a world with zero hero, team, or objective capacity, and
the oracle world has all three at zero.

```
sim_oracle ticks=10000 commands=923 final=0x36e6de56cb662dba stream=0xb6067f3f0955b292 logic=0x5b47e648953a63fc
```

| Gate | Command | Result |
|---|---|---|
| Debug `/WX` | `build-ci/run-debug.cmd` (`cmake --preset ci`, `--config Debug`, full CTest) | **100% tests passed out of 53**, 25.83 s; oracle line above |
| RelWithDebInfo `/WX` | `build-ci/run-reldbg.cmd` | **100% of 53**, 22.71 s; identical oracle line |
| Release `/WX` | `build-ci/run-release.cmd` | **100% of 53**, 23.03 s; identical oracle line |
| ASan | `build-ci/run-asan.cmd` (`--preset debug-asan`, `vcvars64 -vcvars_ver=14.44`, `build-asan-1444`) | **100% of 53**, 42.37 s; identical oracle line; no ASan report |
| clang-cl + UBSan | `tools/check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan` | `CLANG_CL_DETERMINISM=PASS: ubsan=on`; **100% of 7** in the isolation subset; identical oracle line |
| Isolation lints | `sim_boundary`, `sim_compiler_policy` (+ selftest), `sim_isolation_selftest`, `sim_pipeline_owner` | green in every configuration above |
| Binary parity | `sim_binary_parity` | green in Debug, RelWithDebInfo, Release |
| Mutation proof | `sim_determinism_tests --suite sim_determinism` | `controlled divergence tick=4321 field=position_x entity=7`; 4 cases, 178,690 checks |
| Replay byte size | `sim_determinism_tests.cpp:67` | `134812`, unchanged |
| Docs surface | `python <GromCodebase>/tools/docs-surface-lint.py --repo .` | run at wrap |

No Vulkan smoke gate: this slice touches no renderer, `eng_game`, or `eng_assets` code
(plan: Out of scope).

**`sim_pipeline_owner` joined the clang-cl subset.** `tools/check-clang-cl-determinism.ps1`
previously ran only `sim_boundary`, `sim_compiler_policy`, `sim_compiler_policy_selftest`, and
`sim_isolation_selftest` (plus the two UBSan tests). `sim_pipeline_owner` is the gate that
proves `health.current`, the two ledgers, and the match verdict have exactly one writer each,
and a second toolchain reading the same sources is exactly where a stray writer would surface,
so it now rides along - the follow-on named on PR #71. The subset went from 6 tests to 7.
