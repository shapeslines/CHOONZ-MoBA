# Phase 5 M5.2 Slate - Hero Combat and the Unified Effect Pipeline (`m5-hero-combat`)

**Status:** in progress (S0 complete; S1-S6 open)

**Branch:** `lane/moba-m5-hero-combat/20260903` (worktree `GITHUB-ROOT/_worktrees/CHOONZ-MoBA-hero`)

**Plan of record:** `docs/plans/m5-hero-combat.md`

**Base:** integration branch = `origin/main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe`
(M4.1) + `lane/moba-m5.0-map/20260903` (PR #68) + `lane/moba-m5.1-command/20260903`
(PR #69), merged at `d9efd33`. **Both merges were clean** - no conflicts, no manual
resolution, including the two adjacent-line files the lanes were expected to contend for
(`engine/sim/CMakeLists.txt`, `tests/CMakeLists.txt`). The PR opens against `main` and is
stacked behind #68 and #69; it becomes mergeable in that order.

## Goal

Make the mirrored hero playable inside `sim_tick`: a basic attack, the three
proto-design section 3.3 effects (`projectile_damage`, `self_heal`, `area_slow`),
bounded projectile and status state, integer cooldowns and resource, and every
health / cooldown / resource mutation routed through **one** `resolve_effect`
pipeline. Deterministic, hashed, zero new RNG draws, and exactly one reviewed
`SIM_LOGIC_HASH` bump.

## Design decisions recorded

- **Sim-owned POD mirror.** `eng_sim` may include only `engine/sim`, `core`, `math`,
  `serialize`, so it cannot read `content.h`'s `HeroDef`. `engine/sim/include/sim/hero.h`
  carries `SimHeroDef` / `SimActionDef` / `SimEffectDef` as a field-for-field mirror of
  section 3.3. `content.h` **does not exist on this branch** (it lands with PR #70), so the
  cross-check is against the section 3.3 text; the `static_assert` bridge and the
  `HeroDef` to `SimHeroDef` translation belong to the later game-layer slice.
- **One event envelope, not three typed queues.** `DamageEvent` and `DamageEventQueue`
  stay exactly as they are (section 3.4 pins the 12-byte record as the minimum damage
  payload and replay depends on it). Heal, status-applied, status-expired, and death
  travel on a single new `SimEventQueue` carrying a bounded 32-byte `SimEvent` envelope
  in section 3.4 field order. Three separate queues would triple the hash, diff, and
  `SimStateField` surface for one slice and need two more at M5.3.
- **Bounded def table, zero by default.** `SimWorldConfig` gains `hero_def_capacity`,
  `hero_capacity`, `projectile_capacity`, `status_capacity`, `sim_event_capacity`, all
  zero in `sim_world_config_default()`. Zero capacity allocates no arena bytes (skipped
  the way `map_config_is_empty` is), so every existing fixture and the oracle world are
  byte-identical through S1-S3; only S4's hash block moves them.
- **Schedule.** The new systems land at proto-design section 4 steps 5-8:
  `sys_hero_actions`, `sys_projectiles`, `sys_status` before publish;
  `sys_effects_resolve` at step 7; `sys_cooldown_tick` extended at step 8. `sys_status`
  runs **after** `sys_movement` per the section 4 table, so a slow applied on tick N first
  reduces movement on tick N+1 - intended, recorded, not a bug.
- **Two new command kinds inside the frozen 16-byte record.** `SIM_COMMAND_ATTACK = 3`
  (`value_x = fix_from_int(target slot)`, `value_y = 0`, `amount = 0`) and
  `SIM_COMMAND_CAST = 4` (`amount` = action slot; `value_x`/`value_y` interpreted by the
  def table's `target_mode`, never by the wire). `sizeof(SimCommand) == 16`, the replay v1
  codec, and the placeholder command generator are all untouched, so the recorded oracle
  stream stays 923 commands and the replay byte size stays 134,812.
- **Map is not the range authority.** v1 hero combat never calls `map_*`. Range and area
  are squared-distance comparisons on `TransformPool` positions in a 64-bit intermediate.
  The placeholder world's map is empty, so hero-combat fixtures stay independent of map
  capacity. Line of sight and cell-area queries are `m5-lane-objectives`.

## Fence nuances recorded against the plan

- `add_required` in `sim.cpp` rejects a zero-size request, so every zero-capacity pool
  must be skipped in `sim_world_memory_required` and `sim_init` the way the empty map is,
  not passed through with size 0.
- `sim_command_is_canonical(command, player_count, unit_count)` sees no world. Every check
  for the two new kinds must be self-contained; anything needing the def table or a live
  handle goes in `sim_validate_commands` instead.
- `COMMAND_REJECT_COOLDOWN` and `COMMAND_REJECT_RANGE` were **defined but unproduced** by
  M5.1. They are produced by `command_intake_run`, in front of the seam - they are not
  `sim_tick` validation failures, so an out-of-range or on-cooldown pending action is a
  no-op inside `sys_hero_actions`, never a rejected tick.
- Q16.16 squares overflow 32 bits; range tests must widen to 64 bits rather than call
  `fix_mul` twice.
- `check_sim_boundary` bans `float`/`double`, `<vector>`/`<map>`/`<set>`, `new`/`delete`,
  `malloc`, `sqrt`, and any `platform|render|game` include across every new file.

## Baseline evidence (S0)

Run on the untouched integration worktree at `d9efd33`, via
`vcvars64.bat` -> `cmake --preset ci` (`/WX`) -> `cmake --build build-ci --config Debug`
-> `ctest --test-dir build-ci -C Debug --output-on-failure --no-tests=error`:

- **Debug CTest: 100% tests passed, 50/50** (39.16 s). Includes `sim_map` (M5.0),
  `sim_command` (M5.1), `check_sim_boundary`, `check_sim_compiler_policy` (+selftest),
  `check_sim_binary_parity`, `check_sim_isolation_selftest`, `check_sim_ubsan_tripwire`,
  `sandbox_baked_content`, and the shader/CI contract tests.
- `build-ci\tests\Debug\sim_oracle_probe.exe --sim-self-check`:

  ```
  sim_oracle ticks=10000 commands=923 final=0xff4e1ca0c779455b stream=0x218da333e6834496 logic=0xcef8548df2b2a518
  ```

  These are the **M5.0** values (the integration branch carries the M5.0 bump), not the
  pre-M5.0 `0x637628abff59c823` / `0x6f381609f7e59f0c` / `0xab96814425ba80a4` that the
  M5.1 slate recorded against a bare `main` base.
- Replay byte size pin `134812` and mutation pin `tick=4321 field=position_x entity=7`
  hold at this commit.

## Hash bump (S4, the one permitted change) - LANDED

```
SIM_LOGIC_HASH  0xcef8548df2b2a518  ->  0x46e9e287878ba88c
= first 8 bytes (big-endian) of SHA-256 of
  "M5.0|ordered-cache|damage-event-v1|same-tick|explicit-schedule-v1|map-grid-v1|hero-combat-v1"
```

Recipe re-verified on this branch: the M3.2 string reproduces `0xab96814425ba80a4` and the
M5.0 string reproduces `0xcef8548df2b2a518`. The new hash and diff blocks append **after**
the M5.0 map block, and the new `SimStateField` entries append after
`SIM_STATE_FIELD_MAP_LANE_WAYPOINT`, so every existing first-divergence report keeps its
meaning. `0xcef8548df2b2a518` becomes a new rejected historical row in `replay_tests.cpp`.

Fourteen pin sites move; `docs/plans/m5-hero-combat.md` carries the exact file:line table.
The `docs/ROADMAP.md` and `docs/JOURNAL.md` occurrences are past-tense narrative and are
**not** pins.

## Slice ledger

- [x] **S0** Baseline recorded above; merge confirmed clean.
- [x] **S1** `hero.h` defs + validators; `HeroPool` / `ProjectilePool` / `StatusPool`;
  `SimEvent` / `SimEventQueue`; config fields, `sim_world_memory_required`, `sim_init`
  wiring, all zero-capacity by default; `sim_install_hero_def`. Hash untouched, every
  existing test count unchanged.
- [x] **S2** `combat.h/.cpp` `resolve_effect`; basic attack through it; cooldown and
  resource payment; `SIM_COMMAND_ATTACK` through canonicality, validation,
  `sys_apply_commands`, and `command_to_sim`; `sys_cooldown_tick` extended.
- [x] **S3** `sys_hero_actions`, `sys_projectiles`, `sys_status`, `sys_effects_resolve`;
  `SIM_COMMAND_CAST`; the three effects; slow feeding `sys_movement`; `sim_tick` on the
  section 4 schedule.
- [x] **S4** Hash + mirrored diff appended after the map block; `SimStateField` entries;
  `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump; new oracle
  identical in Debug and Release; all fourteen pins moved.
- [ ] **S5** Acceptance: section 7.2 items 3 (full buffer fails the source op, never
  drops), 4 (adding an action def does not move the RNG stream), 5 (all three effects
  through the pipeline; integer cooldown/resource; 3,000-tick duel hashes equal), plus a
  no-pipeline-bypass source scan over `engine/sim/src`.
- [ ] **S6** Gates: `/WX` Debug / RelWithDebInfo / Release, `debug-asan`
  (`vcvars64 -vcvars_ver=14.44`), `check-clang-cl-determinism.ps1 -RequireCompiler
  -RequireUbsan`, isolation lint, binary parity, mutation pin intact. No Vulkan smoke
  (no renderer change). ROADMAP M5.2 status line, ADR-0014 consequences clause, JOURNAL,
  PR body.

## Locked decisions

- No RNG draw of any kind in v1 (no crits) - ADR-0013 keeps `sys_rng_advance` the sole
  unconditional advance, so the tick stream is untouched by this slice.
- `cast_time_ticks` is stored and validated but not simulated; every accepted action fires
  the tick it is accepted. Channel and interrupt state is M5.3.
- No resource regeneration in v1; `resource` only decreases.
- Status stacking is refresh-and-max per (target, effect_type). Additive stacking would be
  a separate hash break.
- `damage_type` is stored, range-validated, and hashed, but selects nothing (mitigation is
  a constant 0 in v1).
- Death emits `SIM_EVENT_DEATH` and routes the entity through `sim_destroy_deferred`;
  respawn is M5.3.

## Slice evidence

### S1 - defs, pools, envelope, config wiring

`hero.h` mirror (`SimEffectDef` 16 B, `SimActionDef` 96 B, `SimHeroDef` 792 B, all
`static_assert`ed) plus `sim_effect_def_valid` / `sim_action_def_valid` /
`sim_hero_def_valid`; `SimHeroDefTable` and `sim_install_hero_def`; `HeroPool`,
`ProjectilePool`, `StatusPool` on the `ComponentPool` core; the `SimEvent` 32-byte
envelope and `SimEventQueue` mirroring `DamageEventQueue`; five `SimWorldConfig`
fields, all zero in `sim_world_config_default()`, skipped in
`sim_world_memory_required` and `sim_init` when zero. Hash untouched.

- `engine_tests --suite sim_hero_combat`: **8 tests, 260 checks, 0 failed**.
- Debug CTest: **100% tests passed, 51/51** (36.0 s) - the baseline 50 plus the new
  `sim_hero_combat` entry, every pre-existing suite unchanged.
- `sim_oracle_probe --sim-self-check` (Debug), **unchanged from S0**:

  ```
  sim_oracle ticks=10000 commands=923 final=0xff4e1ca0c779455b stream=0x218da333e6834496 logic=0xcef8548df2b2a518
  ```

### S2 - unified pipeline, basic attack, cooldown and resource

`combat.h`/`combat.cpp` carry `resolve_effect` (validate -> base magnitude ->
modifiers -> mitigation -> append) plus a mutation-free `resolve_effect_measure`
walk over the identical code, so `sys_hero_actions` pre-flights a whole action and
an event-queue overflow fails the source operation before any state moves.
`sys_combat_resolve` moved into `combat.cpp` beside `sys_effects_resolve`: the two
committers of health now sit in one readable file. `SIM_COMMAND_ATTACK = 3` runs
through `sim_command_is_canonical` (self-contained: exact `fix_from_int(slot)`,
zero `value_y`/`amount`), `sim_validate_commands` (actor has a `HeroPool` row,
target slot is mapped), `sys_apply_commands` (records `pending_*` intent only), and
`command_to_sim` (hero actors only; a non-hero unit keeps the M5.1 `DAMAGE`
placeholder). `sys_cooldown_tick` is the sole decrementer of the basic-attack and
per-slot action cooldowns. Cooldown and range failures are no-ops, never rejected
ticks.

- `engine_tests --suite sim_hero_combat`: **13 tests, 326 checks, 0 failed**.
- Debug CTest: **100% tests passed, 51/51** (31.7 s).
- `sim_oracle_probe --sim-self-check` (Debug), still unchanged:

  ```
  sim_oracle ticks=10000 commands=923 final=0xff4e1ca0c779455b stream=0x218da333e6834496 logic=0xcef8548df2b2a518
  ```

### S3 - CAST and the three effects

`SIM_COMMAND_CAST = 4` (canonicality bounds only the action slot; `value_x`/`value_y`
are read through the def table's `target_mode`), `command_to_sim` resolves
`USE_ACTION`'s `action_id` to a slot through the def table, and
`sim_validate_commands` atomically rejects a packet whose CAST slot is not in the
actor's def. `sys_hero_actions` gained the cast branch (cooldown, resource, and
range are no-ops when they fail); `sys_projectiles` advances in ascending entity
order with an exact integer step (`fix_isqrt64` of a Q32.32 square is Q16.16) and
re-enters `resolve_effect` on impact; `sys_status` decrements and expires rows,
emitting `SIM_EVENT_STATUS_EXPIRED`; `sys_movement` scales the integrated delta by
an `area_slow` scalar. `sim_tick` now runs the full section 4 schedule.

Recorded behaviour: `sys_status` runs after `sys_movement` and
`sys_effects_resolve` commits the status row after `sys_status` has already run, so
a slow applied on tick N keeps its full duration that tick and first reduces
movement on tick N+1.

- `engine_tests --suite sim_hero_combat`: **19 tests, 405 checks, 0 failed**.
- Debug CTest: **100% tests passed, 51/51** (33.8 s).
- `sim_oracle_probe --sim-self-check` (Debug), still unchanged - the placeholder
  world configures no hero, so every new system is a no-op there:

  ```
  sim_oracle ticks=10000 commands=923 final=0xff4e1ca0c779455b stream=0x218da333e6834496 logic=0xcef8548df2b2a518
  ```

### S4 - hash, diff, and the single logic-hash bump

`sim_hash_state` appends, after the M5.0 map block and in this order: the hero def
table (count, then each def's scalars and each action's and effect's scalars),
`HeroPool`, `ProjectilePool`, `StatusPool`, then the `SimEventQueue` read phase and
write phase (count, then per event `tick`, `event_kind`, `payload_size`,
`append_ordinal`, and exactly `payload_size` payload bytes - never the tail
padding). `sim_diff_state` gained `diff_hero_defs` / `diff_heroes` /
`diff_projectiles` / `diff_statuses` / `diff_sim_events` walking the identical
order, and 39 `SimStateField` entries were appended after
`SIM_STATE_FIELD_MAP_LANE_WAYPOINT`, so every pre-existing first-divergence report
keeps its meaning. `canonical_world_valid` now also proves the def table, the three
optional pools (a zero capacity must be a fully absent pool), the envelope queue,
and that every hero row names an installed def.

`SIM_LOGIC_HASH` moved exactly once, `0xcef8548df2b2a518` -> `0x46e9e287878ba88c`.
The recipe was re-verified locally: the M5.0 string reproduces
`0xcef8548df2b2a518` and the M3.2 string reproduces `0xab96814425ba80a4`.

New oracle, **identical in Debug and Release**:

```
sim_oracle ticks=10000 commands=923 final=0xac06a80d7f71b503 stream=0x4209159b82890bcb logic=0x46e9e287878ba88c
```

Both the final state hash and the stream digest move, because the stream digest is
an FNV chain over every tick's state hash.

- `engine_tests --suite sim_hero_combat`: **22 tests, 448 checks, 0 failed**.
- Debug CTest **51/51** (29.0 s); Release CTest **51/51** (29.2 s).
- `sim_determinism` still reports exactly
  `controlled divergence tick=4321 field=position_x entity=7`, and the replay byte
  size pin `134812` is unchanged (the replay v1 codec and the placeholder command
  generator were never touched).

Pins moved (14 sites):

| File | Change |
|---|---|
| `engine/sim/include/sim/replay.h` | `SIM_LOGIC_HASH` -> `0x46e9e287878ba88c` (+ comment) |
| `tests/sim/sim_determinism_tests.cpp` | logic `static_assert` + both oracle `CHECK`s |
| `tests/CMakeLists.txt` | `sim_ubsan_oracle` regex + `replay_inspect` `logic_hash=` regex |
| `tests/sim/check_sim_binary_parity.cmake` | `final=`, `stream=`, `logic=` |
| `tools/check-clang-cl-determinism.ps1` | the whole expected oracle line |
| `README.md` | Debug/Release oracle value |
| `.github/PULL_REQUEST_TEMPLATE.md` | oracle + logic key |
| `tests/sim/replay_tests.cpp` | **added** a rejected historical row for `0xcef8548df2b2a518` |

`docs/ROADMAP.md`, `docs/JOURNAL.md`, and the M3.x / M5.0 slates record historical
values in past-tense narrative and were deliberately left alone.
`docs/decisions/0014` gets its appended clause from Agent C.

### S5-S6 gates

_Pending (Agent C)._
