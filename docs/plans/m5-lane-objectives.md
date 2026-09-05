# Plan — `m5-lane-objectives` (Phase 5 M5.3, sim half)

## Goal

Close the vertical slice inside `sim_tick`: deterministic lane creep waves, a minion state
machine that walks authored waypoints and fights, tower and core objectives that acquire and
fire, a generalized death verdict that carries a killer, gold and XP ledgers fed by authored
`SimEconomyRule` rows, and a core-destruction win condition — all bit-deterministic, all
hashed, zero new RNG draws, and exactly one reviewed `SIM_LOGIC_HASH` bump.

This is the fifth and last slice of the minimum vertical path (proto-design §6:
`m5-map-navigation -> m5-command-replay -> content-typed-payloads -> m5-hero-combat ->
m5-lane-objectives`). After it, §2.2's fixture description is executable: two teams, one lane,
one mirrored hero per team, deterministic creep waves, one tower and one core per team, and a
core-destruction win.

`eng_sim` may include only `engine/sim`, `core`, `math`, `serialize` (compiler policy plus
`check_sim_boundary`: no floats, no STL containers, no `new`/`malloc`, no `assets/`). So the
sim cannot read the asset layer's `MatchDef` / `ObjectiveDef` / `EconomyRule`. It owns a POD
mirror in `engine/sim/include/sim/match.h`, exactly as M5.2 did for `HeroDef` in `hero.h`, and
the field-for-field cross-check is against the **§3.2/§3.3 text**, not against a header. The
`MatchDef` bytes to sim defs translation belongs to a later `eng_game` slice and is out of
scope here.

## Spec sources

- `docs/slate-moba-proto-design.md` §2.2 (the vertical-slice boundary this slice closes),
  §3.2 (`MatchDef`, `TeamDef`, `SpawnDef`, `LaneDef` field order), §3.3 (`ObjectiveDef`,
  `EconomyRule`, and the one-pipeline rule), §3.4 (`SimEvent` kinds and backpressure),
  §4 (the 11-step tick contract), §5 (`invalid-schema`, `stale-entity`, `event-overflow`),
  §6 (this slice's claim and fence), §7.2 items 1, 3, 4, 6.
- `docs/ROADMAP.md` M5.4 execution note — the open items it names are precisely this slice's
  work: **kill credit** and **gold/XP on death**. (Armor mitigation as real data, shields,
  lifesteal, and hero respawn stay open after this slice; see Held questions.)
- `docs/ROADMAP.md` M5.5 — the minion FSM (`lane-push -> attack-nearest`, tie-break by
  `EntityId` then distance, `-> return`), tower target priority (enemy attacking an allied
  hero > nearest minion > nearest hero), fire on cooldown, and its DoD.
- `docs/decisions/0013-sim-rng-draw-policy.md` — no conditional draws. This slice draws
  **zero** RNG values, so the tick stream is untouched.
- `docs/decisions/0014-command-validation-and-backpressure.md` — reject at source on event
  overflow; never drop.
- `docs/plans/m5-hero-combat.md` and `docs/slate-moba-phase5-m5-hero-combat.md` — the
  append-after-the-previous-block hash discipline, the `SimStateField` append rule, the
  zero-capacity-means-absent config pattern, and the pipeline-owner lint this slice extends.
- Code of record on this branch (`a2c5e8d`): `sim.h`, `components.h`, `component_pool.h`,
  `events.h`, `systems.h`, `map.h`, `hero.h`, `combat.h`, `command.h`, `sim_hash.h`,
  `replay.h`, and `sim.cpp` / `combat.cpp` / `systems.cpp` / `sim_hash.cpp`.

## Contract

Everything below is fixed extent, integer only, arena backed, and POD. Q16.16 scalars follow
ADR-0002. Nothing reads platform, renderer, wall clock, or RNG state. Every new struct gets a
`static_assert` on its size so a silent layout change is a build failure, and every reserved
or pad field is **explicit** — there is no implicit tail padding anywhere in this contract,
because the canonical hash must never read a byte the compiler chose.

### `engine/sim/include/sim/team.h` — team identity

```c
typedef uint8_t SimTeamId;
static const SimTeamId SIM_TEAM_NONE = 0xFFu;
static const uint8_t SIM_MAX_TEAMS = 2;          // §3.2: team_count must equal 2

typedef struct TeamView { uint8_t* team; } TeamView;

typedef struct TeamPool {                        // SoA, ComponentPool membership core
    ComponentPool membership;
    uint8_t* team;                               // 0, 1, or SIM_TEAM_NONE
} TeamPool;

size_t team_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool team_pool_init(TeamPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity);
bool team_pool_add(TeamPool* pool, EntityId entity, uint8_t team);
bool team_pool_remove(TeamPool* pool, EntityId entity);
bool team_pool_has(const TeamPool* pool, EntityId entity);
bool team_pool_get(TeamPool* pool, EntityId entity, TeamView* view);
bool team_pool_team(const TeamPool* pool, EntityId entity, uint8_t* out_team);  // const seam
```

**Validity:** a stored `team` is `0`, `1`, or `SIM_TEAM_NONE`; any other byte makes the world
non-canonical. Iteration is `component_pool_ordered_view` (ascending entity index) — never
dense order. `team_pool_team` is the read-only accessor `sim_validate_commands` and
`command_player_owns_unit` use, mirroring `hero_pool_def_index`.

**Membership-ordered** means only that the hash and every walk use the ordered view; the pool
carries no second ordering of its own.

### `engine/sim/include/sim/match.h` — POD mirror of §3.2 / §3.3

```c
static const uint16_t SIM_MAX_OBJECTIVES = 8;
static const uint16_t SIM_MAX_ECONOMY_RULES = 8;
static const uint16_t SIM_MAX_MINIONS_PER_WAVE = 16;

typedef enum SimObjectiveKind : uint8_t {
    SIM_OBJECTIVE_NONE = 0, SIM_OBJECTIVE_TOWER = 1, SIM_OBJECTIVE_CORE = 2,
} SimObjectiveKind;

typedef enum SimTargetPolicy : uint8_t {
    SIM_TARGET_POLICY_NONE = 0,
    SIM_TARGET_POLICY_TOWER = 1,     // hero-attacker > nearest minion > nearest hero
} SimTargetPolicy;

typedef enum SimAwardKind : uint8_t {
    SIM_AWARD_NONE = 0, SIM_AWARD_GOLD = 1, SIM_AWARD_XP = 2,
} SimAwardKind;

typedef enum SimAwardSource : uint8_t {
    SIM_AWARD_SOURCE_NONE = 0,
    SIM_AWARD_SOURCE_HERO_KILL = 1,
    SIM_AWARD_SOURCE_MINION_KILL = 2,
    SIM_AWARD_SOURCE_OBJECTIVE_DAMAGE = 3,       // accepted and hashed; awards nothing in v1
    SIM_AWARD_SOURCE_OBJECTIVE_DESTROYED = 4,
} SimAwardSource;

typedef struct SimObjectiveDef {     // mirrors ObjectiveDef, field for field
    uint64_t objective_id;           // AssetId
    uint8_t  owner_team;             // 0 or 1
    uint8_t  kind;                   // SimObjectiveKind, TOWER or CORE
    uint16_t reserved;               // must be 0
    int32_t  max_health;             // > 0
    int32_t  armor;                  // >= 0; mitigation is a constant 0 in v1
    uint8_t  target_policy;          // SimTargetPolicy
    uint8_t  pad[3];                 // must be 0
} SimObjectiveDef;                   // 24 bytes, align 8

typedef struct SimEconomyRule {      // mirrors EconomyRule, field for field
    uint8_t  award_kind;             // SimAwardKind, non-NONE
    uint8_t  source_kind;            // SimAwardSource, non-NONE
    uint16_t reserved;               // must be 0
    uint32_t award_amount;           // may be 0 (a rule that awards nothing is legal)
} SimEconomyRule;                    // 8 bytes, align 4

typedef struct SimMinionDef {        // no §3.2/§3.3 counterpart; the creep archetype
    uint64_t archetype_id;           // must equal MapLane.creep_archetype_id of its lane
    int32_t  max_health;             // > 0
    mm::fix  move_speed;             // > 0, Q16.16 world units per SECOND (see below)
    mm::fix  attack_range;           // > 0
    int32_t  attack_magnitude;       // > 0
    uint16_t attack_cooldown_ticks;  // > 0
    uint16_t reserved;               // must be 0
    uint32_t reserved2;              // must be 0; keeps the record 32 bytes with no
} SimMinionDef;                      // implicit tail padding. 32 bytes, align 8

typedef struct SimTeamDef {          // the §3.2 TeamDef reduced to sim-resolvable indices
    uint8_t  team_id;                // must equal its array index
    uint8_t  lane_id;                // must name a lane present in world->map
    uint16_t spawn_ordinal;          // argument to map_find_spawn
    uint16_t tower_objective_index;  // < objective_count
    uint16_t core_objective_index;   // < objective_count
    uint32_t tower_cell;             // in bounds, MAP_CELL_OBJECTIVE flagged
    uint32_t core_cell;              // in bounds, MAP_CELL_OBJECTIVE flagged
    uint8_t  waypoint_reverse;       // 0 or 1: walk the lane's waypoints descending
    uint8_t  pad[3];                 // must be 0
} SimTeamDef;                        // 20 bytes, align 4

typedef struct SimMatchDef {
    uint16_t team_count;             // 0 (absent) or exactly 2
    uint16_t objective_count;        // <= SIM_MAX_OBJECTIVES
    uint16_t economy_count;          // <= SIM_MAX_ECONOMY_RULES
    uint16_t minions_per_wave;       // <= SIM_MAX_MINIONS_PER_WAVE
    SimMinionDef  creep;
    SimTeamDef    teams[SIM_MAX_TEAMS];
    SimObjectiveDef objectives[SIM_MAX_OBJECTIVES];
    SimEconomyRule  economy[SIM_MAX_ECONOMY_RULES];
} SimMatchDef;                       // 336 bytes, align 8

static_assert(sizeof(SimObjectiveDef) == 24u, ...);
static_assert(sizeof(SimEconomyRule)  ==  8u, ...);
static_assert(sizeof(SimMinionDef)    == 32u, ...);
static_assert(sizeof(SimTeamDef)      == 20u, ...);
static_assert(sizeof(SimMatchDef)     == 336u, ...);

```

**Correction (S5).** `move_speed` is a **velocity**, not a per-tick displacement. S2 writes it
straight into the Velocity row, and `sys_movement` is the sole integrator: it multiplies by
`SIM_DT_FIXED`, so the per-tick step is `fix_mul(move_speed, SIM_DT_FIXED)`. At 30 Hz
`SIM_DT_FIXED` is 2184, so a `move_speed` of `fix_from_int(15)` steps 32760, not 32768 — a
creep never lands exactly on a lane cell centre, and any range authored as *exactly* the
perpendicular distance to an objective cell will miss forever. Author creep ranges with slack.
(`match.h`'s field comment still reads "per tick"; it is out of this slice's fence and is
corrected in the M5.6 pointer sweep.)

```c
bool sim_objective_def_valid(const SimObjectiveDef* def);
bool sim_economy_rule_valid(const SimEconomyRule* rule);
bool sim_minion_def_valid(const SimMinionDef* def);
bool sim_match_def_valid(const SimMatchDef* def);                  // self-contained
bool sim_match_def_valid_against_map(const SimMatchDef* def, const MapGrid* map);
bool sim_install_match_def(SimWorld* world, const SimMatchDef* def);
```

**Zero-default is feature-absent.** `SimMatchDef` is a by-value member of `SimWorld`, not an
arena table, because it is exactly one bounded record. `sim_world_config_default()` leaves it
zeroed, `team_count == 0` means "no match configured", and every wave, tower, death-credit,
economy, and match-over path returns early. **Every existing fixture, `sim_determinism_tests`,
and the oracle world therefore stay byte-identical through S1–S3; only S4's hash block moves
them.**

**Validity rules, checked whole and rejected whole (§5 `invalid-schema`):**

- `team_count` is 0 or exactly 2; when 0 every other count must be 0 and the whole record
  must be zero bytes (one absent intent has exactly one byte pattern).
- `objective_count >= 2 * team_count` — each team needs a tower and a core.
- `teams[i].team_id == i`; the two `lane_id`s are equal (one lane in the fixture); the two
  `spawn_ordinal`s differ; `waypoint_reverse` differs between the two teams.
- Every `tower_objective_index` / `core_objective_index` is `< objective_count`, the four are
  pairwise distinct, and each names an objective whose `owner_team` equals the referring
  team and whose `kind` is `TOWER` / `CORE` respectively.
- Every `SimObjectiveDef`: `kind` is TOWER or CORE, `owner_team < team_count`,
  `max_health > 0`, `armor >= 0`, `target_policy` in range, `reserved`/`pad` all zero.
- Every `SimEconomyRule`: both enums non-NONE and in range, `reserved` zero. Duplicate
  `(award_kind, source_kind)` pairs are **rejected** — otherwise rule order would be the only
  thing separating two awards and a re-order would be a silent behaviour change.
- Trailing unused `teams` / `objectives` / `economy` slots must be **all zero bytes**.
- `sim_match_def_valid_against_map` additionally requires: `lane_id` resolves to a lane in
  `world->map`; that lane's `creep_archetype_id` equals `creep.archetype_id`; its
  `waypoint_count >= 2`; `map_find_spawn(map, spawn_ordinal, &cell)` succeeds for both teams
  and returns two different cells; every `tower_cell` / `core_cell` is in bounds and carries
  `MAP_CELL_OBJECTIVE`. Install fails without mutation when either check fails.

`sim_install_match_def` runs before the first tick and is **immutable while ticking** — a
mid-match def change is a schema break, not a hash bump — exactly like `sim_install_hero_def`.
It also creates nothing: spawning the objective entities is the caller's job through
`sim_spawn_objective` (below), so install stays a pure validated copy.

### Components — SoA, sparse-set, generational, bounded

All three follow `TransformPool` exactly: `ComponentPool` membership plus typed parallel
arrays, `*_memory_required` / `*_init` / `*_add` / `*_remove` / `*_has` / `*_get`, atomic
failure (arena offset unchanged), and capacity zero meaning the whole struct stays zeroed with
no arena bytes.

```c
typedef enum SimMinionState : uint8_t {
    SIM_MINION_PUSH = 0, SIM_MINION_ATTACK = 1, SIM_MINION_RETURN = 2,
} SimMinionState;

typedef struct MinionPool {
    ComponentPool membership;
    uint8_t*  lane;              // MapLane.lane_id this minion walks
    uint8_t*  waypoint_index;    // < lane waypoint_count
    uint8_t*  state;             // SimMinionState
    EntityId* target;            // HANDLE_NULL unless state == ATTACK
    uint32_t* attack_cooldown;   // ticks remaining; sys_cooldown_tick is the sole decrementer
} MinionPool;

typedef enum SimObjectiveState : uint8_t {
    SIM_OBJECTIVE_ALIVE = 0, SIM_OBJECTIVE_DESTROYED = 1,
} SimObjectiveState;

typedef struct ObjectivePool {
    ComponentPool membership;
    uint16_t* def_index;         // < match.objective_count
    uint8_t*  owner_team;        // must equal match.objectives[def_index].owner_team
    uint8_t*  kind;              // must equal that def's kind
    uint32_t* attack_cooldown;
    uint8_t*  state;             // SimObjectiveState
} ObjectivePool;
```

`owner_team` and `kind` are denormalized from the def so every AI walk is one array read
rather than a def-table indirection; `canonical_world_valid` enforces the agreement, so the
duplication can never drift.

```c
typedef struct SimLedger { int32_t gold[SIM_MAX_TEAMS]; int32_t xp[SIM_MAX_TEAMS]; } SimLedger;
typedef struct SimMatchState {
    uint8_t  over;               // 0 or 1
    uint8_t  winner;             // team id when over, SIM_TEAM_NONE otherwise
    uint16_t reserved;           // must be 0
    uint32_t end_tick;           // the tick `over` was set; 0 while not over
} SimMatchState;                 // 8 bytes, align 4
static_assert(sizeof(SimLedger) == 16u, ...);
static_assert(sizeof(SimMatchState) == 8u, ...);
```

Both are by-value members of `SimWorld` (no capacity, no arena). **Ledger accumulation
saturates**: an award clamps at `INT32_MAX` and never wraps — signed overflow is UB and would
trip the UBSan gate, and a wrapped ledger is not a determinism-safe state.

`HealthPool` gains one parallel array, `EntityId* last_damage_source`, initialized to
`HANDLE_NULL` by `health_pool_add`. It is **written only in `combat.cpp`**, on the damage
commit inside `sys_combat_resolve`, and is the sole kill-credit input. It is hashed (it is
authoritative between steps 6 and 7) and it is cleared to `HANDLE_NULL` by
`health_pool_remove`, so a recycled entity index can never inherit a stale killer.

### `SimWorldConfig` — three new capacities, all zero by default

```c
uint16_t team_capacity;       // TeamPool rows
uint16_t minion_capacity;     // MinionPool rows
uint16_t objective_capacity;  // ObjectivePool rows
```

All zero in `sim_world_config_default()`. As in M5.2, `add_required` rejects a zero-size
request, so a zero-capacity pool must be **skipped** in `sim_world_memory_required` and
`sim_init` (the way `map_config_is_empty` is), never passed through with size 0.

### Events — four kinds appended to the existing envelope

No new queue. `SimEventKind` gains four values; `SIM_EVENT_PAYLOAD_MAX` stays 16 and
`sizeof(SimEvent)` stays 32, so the queue, its phases, its canonicality predicate, and the
reject-at-source policy are untouched.

```c
SIM_EVENT_OBJECTIVE_DAMAGED   = 5,   // payload_size 16
SIM_EVENT_OBJECTIVE_DESTROYED = 6,   // payload_size 12
SIM_EVENT_ECONOMY             = 7,   // payload_size 16
SIM_EVENT_MATCH_OVER          = 8,   // payload_size 12
```

**Payload layouts (explicit little-endian, byte offsets).** The whole envelope keeps one
standing rule this slice makes explicit and enforces in `sim_event_is_canonical`: **offset 0
is always a `u32` `EntityId` slot**, `HANDLE_NULL` when the kind names no entity. That is what
today's `sys_effects_resolve` already assumes when it reads
`sim_event_payload_u32(&event, 0u)` for every kind before branching, so preserving it is a
correctness requirement, not a style choice.

| Kind | 0..3 | 4..7 | 8..11 | 12..15 |
|---|---|---|---|---|
| `OBJECTIVE_DAMAGED` (16) | `target` EntityId | `source` EntityId | `amount` i32 | `remaining_health` i32 |
| `OBJECTIVE_DESTROYED` (12) | `target` EntityId | `killer` EntityId | `owner_team` u8, `kind` u8, `reserved` u16 | — |
| `ECONOMY` (16) | `subject` EntityId | `team` u8, `award_kind` u8, `source_kind` u8, `reserved` u8 | `amount` u32 | `ledger_total` i32 |
| `MATCH_OVER` (12) | `HANDLE_NULL` | `winner` u8, `reason` u8, `reserved` u16 | `end_tick` u32 | — |

Every byte past `payload_size` stays zero (already enforced). `reason` in `MATCH_OVER` is `1`
= core destroyed; it is the only value v1 produces, and the field exists so a later timeout or
surrender rule is a data change rather than a payload change.

**`SIM_EVENT_DEATH` is not widened.** Its payload is already 8 bytes — `target` at offset 0
and `source` at offset 4 — and `sim_event_make_death` already takes a source. Today
`combat.cpp` passes `EntityId{HANDLE_NULL}`. This slice simply **populates** it from
`last_damage_source`, for heroes, minions, and objectives alike. No payload change, no size
change, no new builder.

New builders alongside the existing four, same explicit-field style:

```c
SimEvent sim_event_make_objective_damaged(uint64_t tick, EntityId target, EntityId source,
                                          int32_t amount, int32_t remaining_health);
SimEvent sim_event_make_objective_destroyed(uint64_t tick, EntityId target, EntityId killer,
                                            uint8_t owner_team, uint8_t kind);
SimEvent sim_event_make_economy(uint64_t tick, EntityId subject, uint8_t team,
                                uint8_t award_kind, uint8_t source_kind,
                                uint32_t amount, int32_t ledger_total);
SimEvent sim_event_make_match_over(uint64_t tick, uint8_t winner, uint8_t reason,
                                   uint32_t end_tick);
```

### Systems — five new, one moved

```c
bool sys_waves(SimWorld* world);
bool sys_minion_ai(SimWorld* world);
bool sys_tower_ai(SimWorld* world);
bool sys_death(SimWorld* world);
bool sys_economy(SimWorld* world);
```

Each returns `true` immediately when its feature is absent (`match.team_count == 0`, or the
relevant capacity is 0), so a world without a match ticks exactly as it does today. None draws
RNG. Every walk is `component_pool_ordered_view` (ascending entity index); no system iterates
dense order.

**`sys_waves`** — §4 step 1/3 (ingest-adjacent, before commands, because a wave is authored
state and not player intent). The lane clock is
`tick >= lane.first_wave_tick && (tick - lane.first_wave_tick) % lane.wave_interval_ticks == 0`,
guarded on `wave_interval_ticks != 0`. On a wave tick it spawns `match.minions_per_wave`
minions for each team, in **ascending team id, then ascending within-wave ordinal**, each at
`map_find_spawn(&world->map, teams[t].spawn_ordinal, &cell)` converted through
`map_cell_to_world`. Each minion gets a Transform, Velocity, Health (`creep.max_health`),
Team, and Minion row with `state = PUSH`, `waypoint_index` = 0 for a forward team and
`waypoint_count - 1` for a `waypoint_reverse` team, `target = HANDLE_NULL`,
`attack_cooldown = 0`. **A full pool, a full entity manager, or any failed pool add fails the
source operation**: `sys_waves` returns false, `sim_tick` returns false, the wave is not
partially spawned, and nothing is dropped (ADR-0014). Because a partial wave would be worse
than no wave, `sys_waves` **pre-flights** the whole wave (entity headroom plus every pool's
free room) before creating the first entity.

**`sys_minion_ai`** — §4 step 4, the ROADMAP M5.5 FSM.

- `PUSH`: walk the lane waypoints. The next waypoint is `waypoint_index + 1` for a forward
  team and `- 1` for a `waypoint_reverse` team; the waypoint cell centre comes from
  `map_cell_to_world`. Velocity is the normalized direction times `creep.move_speed`, computed
  in fixed point via `mm::fix_isqrt64` on the 64-bit squared distance — never `sqrt`, never a
  float (`check_sim_boundary` bans both). Arrival is squared-distance within one
  `move_speed` step; the terminal waypoint holds position with zero velocity.
- `PUSH -> ATTACK`: an enemy (Team present and different, alive, Health present) within
  `creep.attack_range`, chosen as **nearest by squared distance, tie-broken by ascending
  `EntityId`**. The ROADMAP phrases this as "tie-break by `EntityId` then distance"; the
  operative order is nearest-first with `EntityId` as the tie-break, and this plan pins that
  reading. Candidates are scanned in ordered-view order so the tie-break is total.
- `ATTACK`: zero velocity; when `attack_cooldown == 0`, route one
  `SIM_EFFECT_PROJECTILE_DAMAGE` `SimEffectDef` (magnitude `creep.attack_magnitude`) through
  `resolve_effect` and arm `attack_cooldown = creep.attack_cooldown_ticks`.
- `ATTACK -> RETURN`: the target is dead, stale, or out of range.
- `RETURN -> PUSH`: unconditional on the next tick, re-acquiring the current waypoint. `RETURN`
  exists as a distinct hashed state so "lost its target this tick" is observable rather than a
  silent re-entry into `PUSH`.

**`sys_tower_ai`** — §4 step 4/5. For each ALIVE objective with `target_policy == TOWER`, in
ordered-view order, when `attack_cooldown == 0`: pick a target by ROADMAP M5.5 priority —
(1) an enemy unit that is **attacking an allied hero**, i.e. an enemy `E` for which some live
hero `H` on the tower's own team carries `health(H).last_damage_source == E`. The ROADMAP
phrases the tier as "enemy attacking allied hero" and that direction is what this plan pins:
the predicate is read off the **ally's** kill-credit slot, not off the candidate's. `E` need
not itself be a hero — a creep beating on our carry is exactly what a tower punishes. The
allied-hero scan is `component_pool_ordered_view` over `HeroPool` and decides **membership
only**, never order, so the caller's nearest-then-`EntityId` rule stays the total order;
(2) the nearest enemy minion; (3) the nearest enemy hero — each tier
restricted to entities within the tower's range, and each tier's internal order nearest-first,
tie-broken by ascending `EntityId`. Tier 1 falls through to tier 2 when empty. Firing routes a
`projectile_damage` `SimEffectDef` through `resolve_effect` and arms `attack_cooldown`. **A
tower never moves and never has a Velocity row.** Tower range and damage come from the
objective's def (`armor` is the defensive field; the offensive magnitude and cadence are
`creep`-style constants pinned in `match.h` alongside the basic-attack constants in `hero.h`,
because §3.3's `ObjectiveDef` carries no offensive fields and widening the mirror would break
the field-for-field contract).

**`sys_death`** — §4 step 7. This **replaces the hero-only death walk currently at the tail of
`sys_effects_resolve` in `combat.cpp`** and generalizes it over heroes, minions, and
objectives, in that fixed pool order and ordered-view order within each pool. For each entity
with `health.current <= 0` that is not already pending destroy:

- emit `SIM_EVENT_DEATH` with `source = *health.last_damage_source` (the killer, or
  `HANDLE_NULL` when nothing credited);
- for an objective, additionally set `state = DESTROYED` and emit
  `SIM_EVENT_OBJECTIVE_DESTROYED`;
- route the entity through `sim_destroy_deferred`, except an objective, which is **not**
  destroyed — a destroyed tower or core stays a hashed `DESTROYED` row so its cell and
  ownership remain queryable and its identity is stable for the rest of the match;
- when the destroyed objective's `kind` is `CORE`, and `match_state.over` is still 0, set
  `over = 1`, `winner` = the other team, `end_tick = world->tick`, and emit **exactly one**
  `SIM_EVENT_MATCH_OVER`. A second core falling on the same tick emits nothing further: the
  `over` flag is the guard, and it is checked and set inside the same walk.

**`sys_economy`** — §4 step 7, immediately after `sys_death`. It walks
`world->pending_destroy` in ascending ordinal (deterministic, already hashed) plus the set of
objectives that transitioned to `DESTROYED` this tick, and for each entity that has a
Hero / Minion / Objective row and `health.current <= 0`, resolves the credit:

- killer = `*health.last_damage_source`; if it is `HANDLE_NULL`, not alive, or has no Team
  row, **no award is made** (an unattributed death is legal and awards nothing).
- source kind = `HERO_KILL` / `MINION_KILL` / `OBJECTIVE_DESTROYED` by which pool the dead
  entity belongs to (checked in that order; an entity in two of them is not canonical).
- Every `match.economy[0..economy_count)` row whose `source_kind` matches is applied **in
  authored rule order** to the killer's team ledger (`gold` or `xp` by `award_kind`,
  saturating), and each award emits one `SIM_EVENT_ECONOMY` carrying the post-award
  `ledger_total`.

`OBJECTIVE_DAMAGE` rows are validated, stored, and hashed but produce **no award in v1** — a
per-damage award would have to fire inside the damage committer, widening `combat.cpp`'s
ownership past what the pipeline-owner lint permits. See Held questions.

**No system outside `combat.cpp` writes `health.current`.** Minions and towers damage through
`resolve_effect` exactly like heroes, so §3.3's one-pipeline rule holds unchanged, and
objectives take damage through the same `DamageEvent` path — that is why
`SIM_EVENT_OBJECTIVE_DAMAGED` is emitted by `sys_combat_resolve` in `combat.cpp` (it is a
damage-commit observation), while every other new event is emitted by `objectives.cpp`.

### `sim_tick` order

```
sim_tick:
  0  sim_validate_commands
  1  sys_waves                      (1)  authored spawn clock, before player intent
  2  sys_apply_commands             (3)
  3  sys_movement                   (4)
  4  sys_minion_ai                  (4)
  5  sys_tower_ai                   (4/5)
  6  sys_hero_actions               (5)
  7  sys_projectiles                (5)
  8  sys_status                     (5)
  9  damage_event_queue_publish + sim_event_queue_publish     (6)
 10  sys_combat_resolve             (6)  writes last_damage_source; OBJECTIVE_DAMAGED
 11  sys_effects_resolve            (7)  heals + statuses only; death walk moves out
 12  damage_event_queue_consume + sim_event_queue_consume
 13  sys_death                      (7)
 14  sys_economy                    (7)
 15  sys_cooldown_tick              (8)  extended over MinionPool and ObjectivePool
 16  sys_rng_advance                (9)  unchanged, zero new draws
 17  sys_flush_destroy              (11)
```

`sys_death` and `sys_economy` run **after** consume, so the events they append land in the
write phase and are read on tick *N+1*. That is exactly the cadence the hero death walk
already has today, so this is a preserved property, not a new one — and it is recorded here as
intended rather than discovered later. Because of it, `sys_economy` cannot read this tick's
`DEATH` events through `sim_event_queue_read`; it reads `pending_destroy` and the objective
state transition instead, both of which are already-hashed authoritative state.

`sys_minion_ai` and `sys_tower_ai` sit **before** publish, so everything they append is
committed on the same tick by `sys_combat_resolve` — identical to `sys_hero_actions`.

### Intake

`CommandIntakeConfig.match_over` stops being a caller-supplied placeholder: the driver wires it
from `world->match_state.over`. `command_player_owns_unit` gains a world-aware sibling that
reads `TeamPool` — **player `p` owns team `p`** — used when `team_capacity` is nonzero, and
falls back to the existing slot-range placeholder otherwise, so every M5.1 test keeps its
meaning. The 40-byte `Command`, the 12-byte `CommandReject`, the reason enum, the 16-byte
`SimCommand`, and the replay v1 codec are all **untouched**.

### Hash and diff

`sim_hash.cpp` appends, **after** the M5.2 `SimEvent` write-phase block (which is itself after
the map block, which is after Health), in this order:

1. the `SimMatchDef` — `team_count`, `objective_count`, `economy_count`, `minions_per_wave`,
   then `creep`'s scalars, then each of the first `team_count` `SimTeamDef`s' scalars, then
   each of the first `objective_count` `SimObjectiveDef`s' scalars, then each of the first
   `economy_count` `SimEconomyRule`s' scalars — **explicit fields only**, never the struct
   image, and never a trailing unused slot;
2. `TeamPool`, `MinionPool`, `ObjectivePool` — each as count, then a membership byte per
   allocated entity index and, when present, the exact `EntityId` plus its SoA fields,
   identical to `hash_transform`;
3. `HealthPool.last_damage_source` — hashed **inside the existing Health block**, appended
   after `damage_cooldown` per Health row, because it is a Health field and splitting it out
   would make the first-divergence report lie about where the state lives;
4. `SimLedger` (`gold[0]`, `gold[1]`, `xp[0]`, `xp[1]`);
5. `SimMatchState` (`over`, `winner`, `end_tick`; `reserved` is not hashed because validity
   already pins it to zero).

`canonical_world_valid` gains the matching structural checks: match def validity plus
map agreement, every `TeamPool.team` byte in range, every `MinionPool.waypoint_index` within
its lane and `state` in range, every `ObjectivePool.def_index < objective_count` with
`owner_team` and `kind` agreeing with the def and `state` in range, `winner` consistent with
`over`, and no ledger entry negative.

`SimStateField` gains one entry per hashed field, appended **at the end of the enum** after
`SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD`, so every existing first-divergence report keeps its
meaning. The one exception is `SIM_STATE_FIELD_HEALTH_LAST_DAMAGE_SOURCE`, which is hashed
inside the Health block but still **appended at the end of the enum** — enum position is an
ABI-ish detail, hash position is the contract, and only the latter must be ordered.
`sim_diff_state` gains a mirrored `diff_match_def` / `diff_teams` / `diff_minions` /
`diff_objectives` / `diff_ledger` / `diff_match_state` chain walking the identical order,
plus the `last_damage_source` comparison inside `diff_health`.

**Exactly one `SIM_LOGIC_HASH` bump**, `engine/sim/include/sim/replay.h:21`:

```
0x46e9e287878ba88c  ->  0x5b47e648953a63fc
= first 8 bytes (big-endian) of SHA-256 of
  "M5.0|ordered-cache|damage-event-v1|same-tick|explicit-schedule-v1|map-grid-v1|hero-combat-v1|lane-objectives-v1"
```

Recipe verified on this branch: the M5.0 string reproduces `0xcef8548df2b2a518` and the
current string reproduces `0x46e9e287878ba88c`, so the generator is the same one both
predecessors used. `0x46e9e287878ba88c` becomes a new rejected historical row in
`tests/sim/replay_tests.cpp` (after the `0xcef8548df2b2a518` row at line 122).

### Map

A new authored fixture `assets/maps/lane_slice.mapdesc`, generated by extending
`tests/sim/map_golden.py` with a second description (and a second golden `.inc`) so the C++
encoder stays cross-checked against an independent implementation of the §3.2 field order,
exactly as `lane_test.mapdesc` is today. Shape: one lane, **two `MAP_CELL_SPAWN` cells** whose
ascending cell order gives `map_find_spawn` ordinals 0 and 1, **four `MAP_CELL_OBJECTIVE`
cells** (a tower and a core per team, each nearer its own spawn), and a waypoint chain running
from spawn ordinal 0 to spawn ordinal 1 along `MAP_CELL_LANE` cells. `lane_test.mapdesc`,
`map_golden.inc`, and `map_tests.cpp` are **not modified** — the new fixture is additive, so
M5.0's golden keeps its meaning.

### RNG

**Zero draws.** No wave, FSM, targeting, tie-break, or award consults `pcg32`. Every tie is
broken by a total order over already-hashed state (squared distance, then `EntityId`), which
is what makes the slice replayable without touching the tick stream (ADR-0013).

## Write fence

Agent B may write **only**:

- `engine/sim/include/sim/{team.h,match.h,objectives.h}` and
  `engine/sim/src/{team.cpp,match.cpp,objectives.cpp}` (new)
- `engine/sim/include/sim/{sim.h,components.h,events.h,systems.h,sim_hash.h}` and
  `engine/sim/src/{sim.cpp,components.cpp,events.cpp,systems.cpp,sim_hash.cpp}` — additive only
- `engine/sim/src/combat.cpp` — three changes only: write `last_damage_source` on the damage
  commit, emit `SIM_EVENT_OBJECTIVE_DAMAGED` for a damaged objective, and **remove** the
  hero-only death walk from the tail of `sys_effects_resolve` (it moves to `objectives.cpp`)
- `engine/sim/include/sim/replay.h` — the `SIM_LOGIC_HASH` constant only (S4)
- `engine/sim/src/command.cpp` and `engine/sim/include/sim/command.h` — the team-aware
  ownership seam only
- `engine/sim/CMakeLists.txt`, `tests/CMakeLists.txt` (source lines, `add_test(NAME
  sim_lane_objectives …)`, and the S4 pin lines)
- `tests/sim/lane_objectives_tests.cpp` (new), `tests/sim/map_golden.py` (additive second
  fixture), `assets/maps/lane_slice.mapdesc` and its golden `.inc` (generated)
- `tests/sim/check_sim_pipeline_owner.cmake` — two new rules only (below)
- the S4 pin sites only, exactly as listed in the ledger.

Never touched: `engine/render`, `engine/game`, `engine/assets`, `engine/net`, `tools/cooker`,
`sim_config.h`, `SIM_MAX_UNITS`, `map.h`/`map.cpp`, `hero.h`/`hero.cpp`, the replay byte
format (`replay.cpp`), the `Command` / `CommandReject` / `SimCommand` records, the placeholder
command generator, `assets/maps/lane_test.mapdesc`, `tests/sim/map_golden.inc`,
`tests/sim/map_tests.cpp`, `docs/ROADMAP.md` historical rows, `github-board.md`,
`master-ledger.jsonl`.

### Pipeline-owner lint — two new rules

`tests/sim/check_sim_pipeline_owner.cmake` gains two rules with the same probe-line self-test
every existing rule has, because this slice introduces two new pieces of state whose "no other
call site exists" property no runtime test can prove:

| Rule | Pattern (assignment only) | Owner |
|---|---|---|
| `ledger` | `(gold\|xp)(\[[^]]*\])?${ASSIGN}` | `objectives.cpp` |
| `match_state` | `[.>](over\|winner\|end_tick)${ASSIGN}` | `objectives.cpp` |

`health.current`'s owner stays `combat.cpp` **unchanged** — `sys_death` never writes health,
it only reads it, appends events, and defers destruction. `last_damage_source` is covered by
extending the existing `health` rule's pattern to `health\.(current|last_damage_source)`, which
keeps its owner `combat.cpp` and needs no new rule. `cooldown_decrement`'s owner stays
`systems.cpp`; `cooldown_set` gains `objectives.cpp` alongside `hero.cpp`/`components.cpp`,
because minion and tower cooldowns are armed there.

## Out of scope

Renderer, `eng_game`, and `eng_assets` work of any kind (so **no Vulkan smoke gate**); the
replay byte format and replay v2; `sim_config.h` and `SIM_MAX_UNITS`; the `HeroDef` /
`ObjectiveDef` / `MatchDef` bytes to sim defs translation and its `content.h` `static_assert`
bridge (`eng_game`, next slice); flow fields and A\* (M5.1 navigation stays its own milestone);
hero and minion respawn; armor mitigation as real data; shields and lifesteal; item shop and
gold spend; level-up and XP sinks; neutral objectives; fog of war (M5.6); per-command rejection
inside `sim_tick` (M6.0, ADR-0014); any RNG draw.

## Slice ledger

- **S0** Baseline on the integration branch: full Debug build plus CTest plus oracle recorded.
- **S1** `team.h`/`team.cpp` and `match.h`/`match.cpp`: `SimTeamId`, `TeamPool`, the five POD
  defs plus every validator, `sim_install_match_def`, `sim_spawn_objective`; `MinionPool` /
  `ObjectivePool` / `SimLedger` / `SimMatchState` and `HealthPool.last_damage_source` in
  `components.{h,cpp}`; the three config capacities plus `sim_world_memory_required` plus
  `sim_init` wiring, all zero-capacity by default. Hash untouched. Full Debug CTest green with
  every existing count and the oracle **unchanged**.
- **S2** `objectives.{h,cpp}`: `sys_waves` and `sys_minion_ai` (waypoint movement with
  `fix_isqrt64`, the PUSH/ATTACK/RETURN FSM, nearest-then-`EntityId` tie-break); the four new
  `SimEventKind` values and their builders in `events.{h,cpp}`; `sys_cooldown_tick` extended
  over `MinionPool`; `sim_tick` gains `sys_waves` and `sys_minion_ai`.
- **S3** `sys_tower_ai`; `last_damage_source` and `SIM_EVENT_OBJECTIVE_DAMAGED` in
  `combat.cpp`; the death walk moved out of `sys_effects_resolve` into the generalized
  `sys_death` (heroes, minions, objectives, killer-carrying `DEATH`,
  `OBJECTIVE_DESTROYED`, one-shot `MATCH_OVER`); `sys_economy`; the team-aware intake seam;
  `sim_tick` on the full order above.
- **S4** Hash and diff blocks appended after the `SimEvent` write-phase block;
  `last_damage_source` folded into the Health block; `SimStateField` entries appended;
  `canonical_world_valid` extended; the single `SIM_LOGIC_HASH` bump to
  `0x5b47e648953a63fc`; new oracle from `sim_oracle_probe --sim-self-check` in Debug **and**
  Release; the `0x46e9e287878ba88c` rejected row added; every pin below moved.
- **S5** Acceptance (Agent C): §7.2 items 6, 1, 3, 4 as spelled out under Acceptance, on the
  `lane_slice.mapdesc` fixture, plus the extended pipeline-owner lint.
- **S6** Gates (Agent C): `/WX` Debug / RelWithDebInfo / Release, `debug-asan`
  (`-vcvars_ver=14.44`), `check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan`,
  isolation lint, binary parity, mutation still `tick=4321 field=position_x entity=7`; slate
  plus ROADMAP M5.3/M5.4/M5.5 status lines plus JOURNAL plus PR body.

### S4 pin sites (exact, verified on this branch at `a2c5e8d`)

| File:line | Current value | Action |
|---|---|---|
| `engine/sim/include/sim/replay.h:21` | `0x46e9e287878ba88c` | to `0x5b47e648953a63fc` |
| `tests/sim/sim_determinism_tests.cpp:25` | `static_assert(SIM_LOGIC_HASH == 0x46e9e287878ba88cULL` | new key |
| `tests/sim/sim_determinism_tests.cpp:67` | `CHECK(size == 134812)` | **re-verify**; expected unchanged |
| `tests/sim/sim_determinism_tests.cpp:68` | `0xac06a80d7f71b503ULL` | new final oracle |
| `tests/sim/sim_determinism_tests.cpp:115` | `0xac06a80d7f71b503ULL` | new final oracle |
| `tests/sim/hero_combat_tests.cpp:937` | `0x46e9e287878ba88cULL` | new key |
| `tests/CMakeLists.txt:148` | `sim_oracle ticks=10000 commands=923 final=0xac06a80d7f71b503 stream=0x4209159b82890bcb logic=0x46e9e287878ba88c` | whole line |
| `tests/CMakeLists.txt:222` | `logic_hash=0x46e9e287878ba88c` | new key |
| `tests/sim/check_sim_binary_parity.cmake:40` | `final=0xac06a80d7f71b503` | new final |
| `tests/sim/check_sim_binary_parity.cmake:41` | `stream=0x4209159b82890bcb` | new stream (it is a digest **over** the canonical hash — see below) |
| `tests/sim/check_sim_binary_parity.cmake:42` | `logic=0x46e9e287878ba88c` | new key |
| `tools/check-clang-cl-determinism.ps1:132` | full oracle line | whole line |
| `.github/PULL_REQUEST_TEMPLATE.md:9` | `0xac06a80d7f71b503`, logic `0x46e9e287878ba88c` | both |
| `README.md:26` | `0xac06a80d7f71b503` | new final |
| `tests/sim/replay_tests.cpp:122` | `{12, 0xcef8548df2b2a518ULL, …}` | **add** a row for `0x46e9e287878ba88c` |

**Correction (S5, verified in code).** An earlier draft of this plan called the oracle
`stream=` field an RNG stream and expected it to hold still across S4. It is not an RNG
stream. `engine/sim/src/oracle.cpp:44` folds **every per-tick `sim_hash_state`** into
`hash_stream_digest` with `hash_u64_le`, so `stream` is a digest *over* the canonical state
hash and therefore moves with **every** canonical-hash bump — as it did at M5.0 and again at
M5.2. Re-pinning it at S4 is correct and expected, not drift.

The real `rng-drift` rule, which this slice does obey: `mm::pcg32_next` has **exactly one call
site** in the engine, `sys_rng_advance` in `engine/sim/src/systems.cpp`, and it is advanced
unconditionally once per tick (ADR-0007, ADR-0013). The RNG state after N ticks is therefore a
function of the seed and N alone — independent of any lane-objectives config, of whether a
match is installed, and of how many minions or awards a tick produced. `rng-drift` means a
**second** `pcg32_next` call site appears, or `world->rng` after N ticks differs between two
worlds that share a seed. Acceptance test §7.2 item 4 asserts exactly that, by comparing
`world->rng` fields directly rather than by reading the oracle `stream=` field.

Not pins — do **not** rewrite: `docs/ROADMAP.md` and `docs/JOURNAL.md` historical values, and
the M3.x / M5.0 / M5.2 slates, which record past-tense narrative.
`tests/sim/replay_tests.cpp:123` uses `SIM_LOGIC_HASH ^ 1` and needs no change.

## Acceptance

1. **§7.2 item 6** — on the `lane_slice.mapdesc` fixture with a full `SimMatchDef`, creep
   waves spawn on the authored lane clock, walk the waypoints, reach the opposing tower, and
   destroy it and then the core; core destruction sets `match_state.over = 1` with the correct
   `winner`, and the run emits `OBJECTIVE_DAMAGED`, `OBJECTIVE_DESTROYED`, `ECONOMY`, and
   exactly one `MATCH_OVER` in a stable, asserted order. Heroes idle throughout.
2. **§7.2 item 1** — two identical runs of that scripted match produce identical per-tick state
   hashes, identical event order, and identical replay result, in Debug **and** Release.
3. **§7.2 item 3** — a `SimEventQueue` or `DamageEventQueue` with no room for a whole wave, a
   whole death, or a whole award fails the **source operation** and therefore the tick; no
   event is dropped and no wave is partially spawned; the arena offset and the state hash are
   unchanged after the rejection.
4. **§7.2 item 4** — two worlds differing only by an extra `SimEconomyRule` row run 3,000 ticks
   of the acceptance match: their ledgers differ (the extra rule pays), their `world->rng`
   fields are **identical** at the end, and a second pair sharing the identical config hashes
   equal. The RNG comparison is made against `world->rng` directly, never against the oracle
   `stream=` field, which is a digest over the canonical hash rather than an RNG trace.
5. Ledger saturation: an award that would exceed `INT32_MAX` clamps, and the clamped world
   still hashes deterministically.
6. Kill credit: a hero killed by an enemy minion credits the minion's team; an unattributed
   death (`last_damage_source == HANDLE_NULL`) awards nothing and is not an error.
7. `sim_oracle_probe --sim-self-check` prints one identical new oracle line in Debug,
   RelWithDebInfo, and Release; binary parity and clang-cl/UBSan reproduce it.
8. The mutation proof still reports exactly `tick=4321 field=position_x entity=7`.
9. Isolation: `check_sim_boundary`, compiler policy (plus selftest), binary parity, isolation
   selftest, UBSan tripwire, and the extended `check_sim_pipeline_owner` are green over the
   new files in every configuration.

## Held questions (defaults applied unless the owner overrules)

- **Flow fields.** Minions follow authored lane waypoints only. No flow field, no A\*; M5.1
  navigation remains its own milestone and this slice must not pre-empt its hash surface.
  — *held*
- **Respawn.** None. A dead hero or minion is destroyed and does not return; a destroyed
  objective stays a `DESTROYED` row forever. Respawn timers are M5.4's open item. — *held*
- **Armor mitigation.** `armor` is stored, validated, and hashed; mitigation is a constant 0,
  exactly as in M5.2. Turning it into `amount * 100/(100+armor)` is a hash break and gets its
  own bump. — *held*
- **XP sink.** `xp` is an observable ledger with no level-up, no stat effect, and no spend
  (§2.2 says so explicitly). — *held*
- **Neutral objectives.** None. Every objective has `owner_team` 0 or 1. — *held*
- **Fog of war.** None; every AI sees every entity. Vision is M5.6. — *held*
- **`OBJECTIVE_DAMAGE` economy rows.** Accepted by the validator and hashed, but they award
  nothing in v1 — awarding per damage tick would put a ledger write inside the damage
  committer and widen `combat.cpp`'s ownership. — *held*
- **Tower offensive tuning.** Range, magnitude, and cadence are constants in `match.h`, not
  `ObjectiveDef` fields, because §3.3's `ObjectiveDef` carries none and the mirror is
  field-for-field. — *held*
- **Heroes in the acceptance match.** Idle. They exist, carry Team rows, and can be killed, but
  they issue no commands, so the match outcome is decided entirely by waves and towers and the
  test is not hostage to hero tuning. — *held*

## Failure states

| Failure | Trigger here | Disposition |
|---|---|---|
| `invalid-schema` | Bad enum, count over cap, index out of range, duplicate economy pair, nonzero reserved/pad, nonzero trailing slot, def/map disagreement | `sim_install_match_def` rejects without mutation |
| `stale-entity` | An AI target or a killer handle not alive at the exact generation | Treated as absent; the FSM re-acquires or awards nothing. Never a tick failure |
| `event-overflow` | Any queue's write phase cannot hold a whole wave, death, or award | Source operation fails, `sim_tick` returns false, world and arena unchanged; nothing dropped (ADR-0014) |
| `hash-drift` | Oracle final hash moves in any slice other than S4 | Stop; the bump is a single reviewed change |
| `rng-drift` | A second `mm::pcg32_next` call site, or `world->rng` after N ticks differing between two worlds sharing a seed | Forbidden by ADR-0013; this slice draws zero. (The oracle `stream=` field is **not** this signal — it is a digest over the canonical hash and moves with every hash bump) |
| `pipeline-bypass` | A `health.current`, ledger, or match-state write outside its owner file | `check_sim_pipeline_owner` fails the build |
| `map-coupling` | `sys_waves` / `sys_minion_ai` calling `map_*` outside `map_find_spawn`, `map_cell_to_world`, `map_lane_waypoints`, and `map_cell_coords` | Out of contract; navigation stays waypoint-only |
| `replay-mismatch` | Header, command, event, or state hash differs on replay | Stop at the first mismatch and report tick/field |

## Branch

`lane/moba-m5-lane-objectives/20260904`, worktree
`GITHUB-ROOT/_worktrees/CHOONZ-MoBA-objectives`, at `a2c5e8d` = `origin/main`
(`4f66af17e9b8ad65c9b5238426775f656d2811fe`, M4.1) plus `lane/moba-m5.0-map` (PR #68) plus
`lane/moba-m5.1-command` (PR #69) plus `lane/moba-m5-hero-combat` (PR #71). The PR opens
against `main` and is stacked behind #68 -> #69 -> #71; it becomes mergeable in that order.
