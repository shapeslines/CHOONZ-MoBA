# Plan — `m5-hero-combat` (Phase 5 M5.2, sim half)

## Goal

Make the mirrored hero playable inside `sim_tick`: a basic attack, three data-defined
effects (`projectile_damage`, `self_heal`, `area_slow`), bounded projectile and status
state, integer cooldowns and resource, and every mutation routed through **one**
`resolve_effect` pipeline — all deterministic, all hashed, with exactly one reviewed
`SIM_LOGIC_HASH` bump.

`eng_sim` may include only `engine/sim`, `core`, `math`, `serialize` (compiler policy +
`check_sim_boundary`: no floats, no STL containers, no `new`/`malloc`, no `assets/`).
So the sim cannot read `content.h`'s `HeroDef`. **`engine/assets/include/assets/content.h`
does not exist on this branch at all** (it lands with PR #70). The sim therefore owns a
POD mirror in `engine/sim/include/sim/hero.h`, and the field-for-field cross-check is
against the **§3.3 text**, not against a header. A later game-layer slice (`eng_game`
links both) translates `HeroDef` bytes into sim defs; a `static_assert` bridge to
`content.h` is deferred to that slice and is **out of scope here**.

## Spec sources

- `docs/slate-moba-proto-design.md` §3.3 (Hero/Action/Effect shapes and the
  one-pipeline rule), §3.4 (`Command`, `CommandReject`, `SimEvent`, event
  backpressure), §4 (the 11-step tick contract), §5 (`invalid-schema`,
  `stale-entity`, `event-overflow`), §7.2 items 3–5.
- `docs/decisions/0013-sim-rng-draw-policy.md` — no conditional draws; v1 hero
  combat draws **zero** RNG values (no crits), so the tick stream is untouched.
- `docs/decisions/0014-command-validation-and-backpressure.md` — atomic packet
  rejection until M6.0; reject-at-source on event overflow, never drop.
- `docs/slate-moba-phase5-m5.0.md` (hash-bump recipe, `SimStateField` append
  discipline, the append-after-Health rule) and `docs/slate-moba-phase5-m5.1.md`
  (the intake sits *in front of* the legacy seam; replay v1 bytes are frozen).
- Code of record on this branch: `sim.h`, `components.h`, `component_pool.h`,
  `events.h`, `systems.h`, `map.h`, `command.h`, `sim_hash.h`, `replay.h`.

## Contract

### `engine/sim/include/sim/hero.h` — POD mirror of §3.3

```c
static const uint16_t SIM_MAX_ACTION_SLOTS = 8;   // §3.3 fixture uses 3
static const uint16_t SIM_MAX_EFFECTS_PER_ACTION = 4;

typedef enum SimEffectType : uint8_t {
    SIM_EFFECT_NONE = 0,
    SIM_EFFECT_PROJECTILE_DAMAGE = 1,
    SIM_EFFECT_SELF_HEAL = 2,
    SIM_EFFECT_AREA_SLOW = 3,
} SimEffectType;

typedef enum SimTargetMode : uint8_t {
    SIM_TARGET_POINT = 0, SIM_TARGET_ENTITY = 1, SIM_TARGET_SELF = 2, SIM_TARGET_AREA = 3,
} SimTargetMode;

typedef struct SimEffectDef {            // mirrors EffectDef, field-for-field
    uint8_t  effect_type;
    uint8_t  damage_type;
    uint16_t duration_ticks;
    int32_t  magnitude;
    mm::fix  radius;                     // radius_q16
    mm::fix  scalar;                     // scalar_q16
} SimEffectDef;                          // 16 bytes

typedef struct SimActionDef {            // mirrors ActionDef
    uint64_t action_id;
    uint8_t  slot;
    uint8_t  target_mode;
    uint16_t effect_count;
    uint32_t cooldown_ticks;
    uint32_t cast_time_ticks;
    uint32_t resource_cost;
    mm::fix  range;                      // range_q16
    mm::fix  projectile_speed;           // projectile_speed_q16
    SimEffectDef effects[SIM_MAX_EFFECTS_PER_ACTION];
} SimActionDef;

typedef struct SimHeroDef {              // mirrors HeroDef (schema_version stays
    uint64_t hero_def_id;                //  in the asset layer, not the sim)
    int32_t  max_health;
    mm::fix  move_speed;
    mm::fix  attack_range;
    uint16_t action_count;
    SimActionDef actions[SIM_MAX_ACTION_SLOTS];
} SimHeroDef;
```

Every struct is a fixed-extent POD with a `static_assert` on `sizeof`. `hero.h`
declares `sim_hero_def_valid` / `sim_action_def_valid` / `sim_effect_def_valid`
(§5 `invalid-schema`: unknown enum, `effect_count > 4`, `action_count > 8`,
negative `max_health`, duplicate slot — reject the def, never partially accept).

### Def table — bounded, in `SimWorld`, zero by default

```c
typedef struct SimHeroDefTable {
    SimHeroDef* defs;      // arena-owned, config.hero_def_capacity entries
    uint16_t    capacity;
    uint16_t    count;
} SimHeroDefTable;
```

`SimWorldConfig` gains `uint16_t hero_def_capacity, hero_capacity,
projectile_capacity, status_capacity;` and `uint32_t sim_event_capacity;` —
**all zero in `sim_world_config_default()`**. Zero capacity means: no arena
bytes (skipped exactly like `map_config_is_empty`, because `add_required`
rejects a zero size), no pools, every hero query reports "absent". Every
existing fixture, `sim_determinism_tests`, and the oracle world therefore stay
byte-identical through S1–S3; only S4's hash block moves them.

Defs are installed through `sim_install_hero_def(world, const SimHeroDef*)`
before the first tick; the table is immutable during ticking (a mid-match def
change is a schema break, not a hash bump).

### Components — SoA, sparse-set, generational, bounded

- `HeroPool` — per hero entity: `def_index (u16)`, `resource (i32)`,
  `basic_attack_cooldown (u32)`, `action_cooldown[SIM_MAX_ACTION_SLOTS] (u32)`,
  `pending_kind (u8)`, `pending_slot (u8)`, `pending_target (EntityId)`,
  `pending_point_x/_y (mm::fix)`. The `pending_*` fields are the tick-local
  intent written by `sys_apply_commands` and consumed by `sys_hero_actions`;
  they are cleared at the end of `sys_hero_actions` **and are hashed** (they are
  authoritative between steps 3 and 5).
- `ProjectilePool` — `source (EntityId)`, `target (EntityId)`,
  `def_index (u16)`, `action_slot (u8)`, `effect_index (u8)`,
  `position_x/_y (mm::fix)`, `speed (mm::fix)`, `remaining_ticks (u16)`.
  Projectiles are entities so they inherit generational identity and the
  `ComponentPool` ordered view; iteration uses `component_pool_ordered_view`
  (ascending entity index), never dense order.
- `StatusPool` — `target (EntityId)` (the pool key), `effect_type (u8)`,
  `remaining_ticks (u16)`, `magnitude (i32)`, `scalar (mm::fix)`,
  `stack_count (u8)`. One row per (target, effect_type): a re-application
  refreshes duration and takes the larger magnitude (deterministic, no list).

All three use the existing `ComponentPool` membership plus typed parallel arrays,
`*_memory_required` / `*_init` / `*_add` / `*_remove` / `*_has` / `*_get`,
following `TransformPool` exactly. Failure is atomic (arena offset unchanged).

### Events — one new envelope queue; `DamageEvent` untouched

**Decision (a): one `SimEventQueue`, not three typed queues.** §3.4 already
specifies a `SimEvent` envelope, and the `DamageEventQueue` pattern
(double-buffered, fixed capacity, explicit publish/read/consume, reject at
source) transfers to it unchanged. Three separate typed queues would triple the
canonical-hash surface, the diff surface, and the `SimStateField` block for one
slice, and would need a fourth and fifth queue at M5.3 (objective, economy,
match). One envelope carries every future kind at the cost of one hash block.

```c
static const uint16_t SIM_EVENT_PAYLOAD_MAX = 16;

typedef enum SimEventKind : uint16_t {
    SIM_EVENT_NONE = 0, SIM_EVENT_HEAL = 1, SIM_EVENT_STATUS_APPLIED = 2,
    SIM_EVENT_STATUS_EXPIRED = 3, SIM_EVENT_DEATH = 4,
} SimEventKind;

typedef struct SimEvent {                 // §3.4 field order, bounded payload
    uint64_t tick;
    uint16_t event_kind;
    uint16_t payload_size;                // <= SIM_EVENT_PAYLOAD_MAX
    uint32_t append_ordinal;
    uint8_t  payload[SIM_EVENT_PAYLOAD_MAX];
} SimEvent;                               // 32 bytes, static_assert'd
```

`SimEventQueue` mirrors `DamageEventQueue` one-for-one (`_memory_required`,
`_init`, `_is_valid`, `_append`, `_publish`, `_read`, `_consume`, plus
`sim_event_is_canonical`). Canonicality requires `event_kind` known,
`payload_size` equal to the fixed size for that kind, **every byte past
`payload_size` zero**, and `append_ordinal` equal to the position in the write
phase (so append order is provably the hash order). The hash reads exactly
`payload_size` bytes — never the tail padding.

`DamageEvent` (12 bytes) and `DamageEventQueue` are the damage wire and are
**not modified**: §3.4 pins them as the minimum damage payload and the replay
seam depends on them. Heals are `SIM_EVENT_HEAL`, not negative damage.
Overflow of either queue fails the **source operation** and therefore the tick
(ADR-0014); nothing is ever dropped or overwritten.

### Systems — inserted at §4 steps 5–8

```
sim_tick:
  0  sim_validate_commands                    (§4 steps 1-2, extended for kinds 3/4)
  1  sys_apply_commands                       (3)  ATTACK/CAST -> HeroPool.pending_*
  2  sys_movement                             (4)  reads StatusPool slow scalar
  3  sys_hero_actions                         (5)  validate -> pay -> spawn/apply
  4  sys_projectiles                          (5)  advance in ordered entity order
  5  sys_status                               (5)  tick durations, expire, emit
  6  damage_event_queue_publish + sim_event_queue_publish   (6)
  7  sys_combat_resolve                       (6)  unchanged
  8  sys_effects_resolve                      (7)  heals/deaths from the envelope
  9  damage_event_queue_consume + sim_event_queue_consume
 10  sys_cooldown_tick                        (8)  extended over HeroPool
 11  sys_rng_advance                          (9)  unchanged, zero new draws
 12  sys_flush_destroy                        (11)
```

**Decision (c), locked:** `sys_status` runs at step 5, i.e. **after**
`sys_movement` at step 4. A slow applied on tick *N* therefore first reduces
movement on tick *N+1*. This is what §4's table dictates and it is recorded as
intended behaviour, not a bug. `sys_movement` reads the slow scalar from
`StatusPool` and multiplies the integrated delta (`fix_mul`), so a world with an
empty `StatusPool` integrates exactly as it does today — the oracle path is
untouched until S4.

Order inside every new system is `component_pool_ordered_view` (ascending
sparse/entity index). No system iterates dense order, and none draws RNG.

### Pipeline — `resolve_effect` is the only mutator

```c
bool resolve_effect(SimWorld* world, EntityId source, EntityId target,
                    const SimEffectDef* effect, mm::fix point_x, mm::fix point_y);
```

Stages, in order: **validate** (both handles alive, required components present,
effect canonical) → **base magnitude** (from the def) → **modifiers** (data-only;
none in v1) → **mitigation** (armor placeholder, always 0 in v1) → **append
event** (`DamageEvent` for damage, `SimEvent` for heal/status) → return. It
appends only; state is committed by `sys_combat_resolve` / `sys_effects_resolve`
after publish, so append order *is* commit order.

**No other function in `engine/sim/src` may write `health.current`,
`hero.resource`, or any `*_cooldown`.** Agent C enforces this with a
source-scanning test in the spirit of `check_sim_boundary`
(`sys_combat_resolve` and `sys_effects_resolve` in `combat.cpp` are the sole
committers; `sys_cooldown_tick` is the sole decrementer).

### Commands — two new kinds inside the frozen 16-byte record

**Decision (d):** `SimCommandKind` gains `SIM_COMMAND_ATTACK = 3` and
`SIM_COMMAND_CAST = 4`. The record, `sizeof(SimCommand) == 16`, the replay v1
codec (which writes every field explicitly), and the placeholder command
generator are **all untouched**, so the recorded oracle stream stays 923
commands of kinds 1 and 2 and the replay byte size stays 134,812.

`sim_command_is_canonical` (signature unchanged — it sees no world, so every
check must be self-contained) extends to:

- `ATTACK`: `value_x == fix_from_int(k)` with `0 <= k < unit_count`
  (the target's unit slot), `value_y == 0`, `amount == 0`.
- `CAST`: `0 <= amount < SIM_MAX_ACTION_SLOTS`; `value_x`/`value_y` are
  unconstrained — their meaning (a point, or `fix_from_int(target slot)` with
  `value_y == 0`, or ignored for self) is selected by the action's
  `target_mode`, which comes from the **def table**, never from the wire, so
  there is no ambiguity and no world lookup inside the canonicality predicate.

`sim_validate_commands` additionally requires, for kinds 3 and 4, that the
actor slot resolves to a live entity with a `HeroPool` row and that the CAST
slot is `< def.action_count` — a failure is an atomic packet reject (ADR-0014).
Cooldown and range are **not** validation failures: they are
`COMMAND_REJECT_COOLDOWN` / `COMMAND_REJECT_RANGE` produced by
`command_intake_run` (the reasons M5.1 defined and left unproduced), and
`sys_hero_actions` treats an out-of-range or on-cooldown pending action as a
no-op, not a tick failure.

`command_to_sim` in `command.cpp` (translation only, no other change):
`COMMAND_KIND_BASIC_ATTACK` now maps to `SIM_COMMAND_ATTACK` when the actor has
a `HeroPool` row and keeps its M5.1 `SIM_COMMAND_DAMAGE` placeholder otherwise;
`COMMAND_KIND_USE_ACTION` maps to `SIM_COMMAND_CAST` and stops returning false.

### Map ownership for range and area

**Decision (e): v1 hero combat never calls `map_*`.** Range and area are pure
fixed-point distance tests on `TransformPool` positions: compare
`dx*dx + dy*dy` against `range*range` in a **64-bit intermediate** (a Q16.16
square overflows 32 bits; use the widening helper, not `fix_mul`). No line of
sight, no cell occupancy, no pathing. The placeholder world's map is empty
(`map_config_is_empty`), so this keeps every hero-combat fixture independent of
map capacity, and it keeps this slice's hash block orthogonal to M5.0's.
Map-gated line of sight and cell-area queries are `m5-lane-objectives`.

### Hash and diff

`sim_hash.cpp` appends, **after** `hash_map` (which is itself after
`hash_health`), in this order: the hero def table (`count`, then each def's
scalars and each action's and effect's scalars), `HeroPool`, `ProjectilePool`,
`StatusPool`, then the `SimEventQueue` read phase and write phase (count, then
per event: `tick`, `event_kind`, `payload_size`, `append_ordinal`, exactly
`payload_size` payload bytes). Each pool encodes count, then a membership byte
per allocated entity index and, when present, the exact `EntityId` plus its SoA
fields — identical to `hash_transform`. `canonical_world_valid` gains the
matching structural checks. `SimStateField` gains one entry per hashed field,
appended at the end of the enum after `SIM_STATE_FIELD_MAP_LANE_WAYPOINT`, so
every existing first-divergence report keeps its meaning; `sim_diff_state` gains
a mirrored `diff_hero` / `diff_projectiles` / `diff_status` / `diff_sim_events`
chain after `diff_map`, walking the identical order.

**Exactly one `SIM_LOGIC_HASH` bump**, `engine/sim/include/sim/replay.h:19`:

```
0xcef8548df2b2a518  ->  0x46e9e287878ba88c
= first 8 bytes (big-endian) of SHA-256 of
  "M5.0|ordered-cache|damage-event-v1|same-tick|explicit-schedule-v1|map-grid-v1|hero-combat-v1"
```

Recipe verified against both predecessors on this branch: the M3.2 string
reproduces `0xab96814425ba80a4` and the M5.0 string reproduces
`0xcef8548df2b2a518`. `0xcef8548df2b2a518` becomes a new rejected historical row
in `tests/sim/replay_tests.cpp`.

## Write fence

Agent B may write **only**:

- `engine/sim/include/sim/{hero.h,combat.h}`, `engine/sim/src/{hero.cpp,combat.cpp}` (new)
- `engine/sim/include/sim/{sim.h,components.h,events.h,systems.h,sim_hash.h}` and
  `engine/sim/src/{sim.cpp,components.cpp,events.cpp,systems.cpp,sim_hash.cpp}` — additive only
- `engine/sim/include/sim/replay.h` — the `SIM_LOGIC_HASH` constant only (S4)
- `engine/sim/src/command.cpp` — `command_to_sim` translation only
- `engine/sim/CMakeLists.txt`, `tests/CMakeLists.txt` (source lines plus `add_test(NAME sim_hero_combat …)`)
- `tests/sim/hero_combat_tests.cpp` (new)
- the S4 pin sites only, exactly as listed in the ledger.

Never touched: `engine/render`, `engine/game`, `engine/assets`, `engine/net`,
`tools/cooker`, `sim_config.h`, `map.h`/`map.cpp`, `command.h`, the replay byte
format (`replay.cpp`), the placeholder command generator, `docs/ROADMAP.md`
historical rows, `github-board.md`, `master-ledger.jsonl`.

## Out of scope

Creeps, waves, towers, cores, victory, gold and XP (`m5-lane-objectives`, M5.3);
snapshots and presentation filtering (M5.4); the `HeroDef` to `SimHeroDef`
translation and its `content.h` `static_assert` bridge (game layer, after #70);
per-command rejection inside `sim_tick` and replay v2 (M6.0, ADR-0014); crits or
any RNG draw; armor as real data; line of sight; navigation and flow fields;
renderer work of any kind (so **no Vulkan smoke gate**).

## Slice ledger

- **S0** Baseline on the integration branch: full Debug matrix plus oracle recorded.
- **S1** `hero.h` defs and validators; `HeroPool` / `ProjectilePool` / `StatusPool` in
  `components.{h,cpp}`; `SimEvent`/`SimEventQueue` in `events.{h,cpp}`; config
  fields plus `sim_world_memory_required` plus `sim_init` wiring, all zero-capacity by
  default; `sim_install_hero_def`. Hash untouched. Full Debug `ctest` green with
  every count unchanged.
- **S2** `combat.h/.cpp`: `resolve_effect`; basic attack through it; cooldown and
  resource payment; `SIM_COMMAND_ATTACK` in `sim_command_is_canonical`,
  `sim_validate_commands`, `sys_apply_commands`, and `command_to_sim`;
  `sys_cooldown_tick` extended.
- **S3** `sys_hero_actions`, `sys_projectiles`, `sys_status`,
  `sys_effects_resolve`; `SIM_COMMAND_CAST`; the three effects; slow feeding
  `sys_movement`; `sim_tick` reordered to the §4 schedule above.
- **S4** Hash and diff blocks appended after the map block; `SimStateField` entries
  appended; `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump;
  new oracle from `sim_oracle_probe --sim-self-check` in Debug **and** Release;
  every pin below moved.
- **S5** Acceptance tests (Agent C): §7.2 items 3, 4, 5 plus the no-bypass source scan.
- **S6** Gates (Agent C): `/WX` Debug / RelWithDebInfo / Release, `debug-asan`
  (`-vcvars_ver=14.44`), clang-cl plus UBSan, isolation lint, binary parity,
  mutation still `tick=4321 field=position_x entity=7`; slate plus ROADMAP plus
  ADR-0014 consequences plus PR body.

### S4 pin sites (exact, verified on this branch at `d9efd33`)

| File:line | Current value | Action |
|---|---|---|
| `engine/sim/include/sim/replay.h:19` | `0xcef8548df2b2a518` | to `0x46e9e287878ba88c` |
| `tests/sim/sim_determinism_tests.cpp:25` | `static_assert(SIM_LOGIC_HASH == 0xcef8548df2b2a518ULL` | new key |
| `tests/sim/sim_determinism_tests.cpp:67` | `CHECK(size == 134812)` | **re-verify**; expected unchanged |
| `tests/sim/sim_determinism_tests.cpp:68` | `0xff4e1ca0c779455bULL` | new final oracle |
| `tests/sim/sim_determinism_tests.cpp:115` | `0xff4e1ca0c779455bULL` | new final oracle |
| `tests/CMakeLists.txt:146` | `sim_oracle ticks=10000 commands=923 final=0xff4e1ca0c779455b stream=0x218da333e6834496 logic=0xcef8548df2b2a518` | whole line |
| `tests/CMakeLists.txt:214` | `logic_hash=0xcef8548df2b2a518` | new key |
| `tests/sim/check_sim_binary_parity.cmake:40` | `final=0xff4e1ca0c779455b` | new final |
| `tests/sim/check_sim_binary_parity.cmake:41` | `stream=0x218da333e6834496` | new stream |
| `tests/sim/check_sim_binary_parity.cmake:42` | `logic=0xcef8548df2b2a518` | new key |
| `tools/check-clang-cl-determinism.ps1:132` | full oracle line | whole line |
| `README.md:26` | `0xff4e1ca0c779455b` | new final |
| `.github/PULL_REQUEST_TEMPLATE.md:9` | `0xff4e1ca0c779455b`, logic `0xcef8548df2b2a518` | both |
| `tests/sim/replay_tests.cpp:121` | `{12, 0xab96814425ba80a4ULL, …}` | **add** a row for `0xcef8548df2b2a518` |

Not pins — do **not** rewrite: `docs/ROADMAP.md:519,522,556,582,583,603,604,656,657`,
`docs/JOURNAL.md:167`, and the M3.x / M5.0 slates all record historical values in
past-tense narrative. `docs/decisions/0014` line "the oracle `0x637628abff59c823`
(M5.0 re-pinned …)" gets one appended clause (Agent C).
`tests/sim/replay_tests.cpp:122` uses `SIM_LOGIC_HASH ^ 1` and needs no change.

## Acceptance

1. `engine_tests --suite sim_hero_combat` green in Debug and Release.
2. §7.2 item 3 — a full `SimEventQueue` / `DamageEventQueue` fails the source
   operation and the tick; no event is dropped; arena bytes and state hash are
   unchanged after the rejection.
3. §7.2 item 4 — two worlds, one carrying an extra action def, produce the same
   `sys_rng_advance` count and the identical RNG stream over 3,000 ticks.
4. §7.2 item 5 — a mirrored hero uses `projectile_damage`, `self_heal`, and
   `area_slow` through `resolve_effect`; cooldown and resource are integer ticks;
   two identical runs of a scripted 3,000-tick duel hash equal every tick.
5. No ability path bypasses the pipeline (source scan over `engine/sim/src`).
6. `sim_oracle_probe --sim-self-check` prints one identical new oracle line in
   Debug, RelWithDebInfo, and Release; binary parity and clang-cl/UBSan reproduce it.
7. The mutation proof still reports exactly `tick=4321 field=position_x entity=7`.
8. Isolation: `check_sim_boundary`, compiler policy (plus selftest), binary parity,
   isolation selftest, UBSan tripwire green over the new files in every config.

## Held questions (defaults applied unless the owner overrules)

- **Cast time.** `cast_time_ticks` is stored and validated but **not simulated** in
  v1 (every action fires the tick it is accepted). Default: defer channel and
  interrupt state to M5.3. — *held*
- **Resource regeneration.** None in v1; `resource` only decreases. Default: a
  regen rate belongs to `SimHeroDef` in a later slice. — *held*
- **Death.** `SIM_EVENT_DEATH` is emitted and the entity is routed through
  `sim_destroy_deferred`; respawn is M5.3. — *held*
- **Status stacking.** Refresh-and-max per (target, effect_type). An additive
  stacking model is a hash break and would need its own bump. — *held*
- **`damage_type`.** Stored, validated in range, and hashed; it selects nothing
  in v1 (mitigation is a constant 0). — *held*

## Failure states

| Failure | Trigger here | Disposition |
|---|---|---|
| `invalid-schema` | Unknown effect/target enum, `effect_count > 4`, `action_count > 8`, duplicate slot, negative `max_health` | `sim_install_hero_def` rejects without mutation |
| `stale-entity` | ATTACK/CAST actor or target not alive at the exact generation | Atomic packet reject in `sim_validate_commands` (ADR-0014) |
| `event-overflow` | Either queue's write phase is full | Source operation fails, `sim_tick` returns false, world unchanged |
| `hash-drift` | Oracle moves in any slice other than S4 | Stop; the bump is a single reviewed change |
| `rng-drift` | Any new `pcg32_next` call | Forbidden by ADR-0013; v1 draws zero |
| `pipeline-bypass` | A direct `health.current` / cooldown / resource write outside `combat.cpp` | Source-scan test fails the build |
| `map-coupling` | Any `map_*` call from hero-combat code | Out of contract; range is distance-only |

## Branch

`lane/moba-m5-hero-combat/20260903`, worktree
`GITHUB-ROOT/_worktrees/CHOONZ-MoBA-hero`, equal to `origin/main` (`4f66af1`) plus
`lane/moba-m5.0-map/20260903` plus `lane/moba-m5.1-command/20260903`, merged
cleanly at `d9efd33` (no conflicts, no manual resolution). The PR opens against
`main`, stacked behind #68 and #69.
