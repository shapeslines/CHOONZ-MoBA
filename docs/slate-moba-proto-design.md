---
type: design-slate
title: "CHOONZ-MoBA M4.1 One-Lane Design Packet"
id: 2026-08-26-moba-proto-design
status: ready
stability: design
lane_id: moba-proto-design
workflow_pattern: workflow.lean-lane-mint/v1
operating_pattern: RAIL
base_commit: 6571ee40adfd7e99a49564a7c0d21d4702ee80c8
main_merge_base: e4b3369b36267dcef9fdbcebe2f67b227959d3bd
---

# CHOONZ-MoBA M4.1 One-Lane Design Packet

**Stack:** C++17, CMake/Ninja Multi-Config, raw Vulkan 1.3, custom ECS, offline
content cooker, 30 Hz fixed-tick deterministic simulation.

**Hand to:** Codex implementation lanes at the CHOONZ-MoBA repository root.

This packet defines the smallest implementation-ready MOBA slice that can be
built on the current custom engine: one deterministic lane, two teams, one
mirrored hero per team, lane creeps, one tower and one core per side, three
data-driven abilities, and replayable authoritative simulation. It changes no
engine code.

## 1. Authority, base, and source ownership

### 1.1 Git evidence

| Authority | Exact evidence | Role |
|---|---|---|
| GromAtlas transport | PR 139, `fc56c2932053a7d4b06fcf229c35642605a18cc0`, branch `lane/ct-program/20260826-mint-packet` | Source packet and invocation only |
| Main baseline | `origin/main@e4b3369b36267dcef9fdbcebe2f67b227959d3bd` | M4.1 merge-base |
| Accepted active line | `6571ee40adfd7e99a49564a7c0d21d4702ee80c8`, `feat: add deterministic mba cooker` | Design base and M4.1 contract |
| Active M4.1 branch | `codex/m4.1-cooker-restart` at `C:\Users\doton\Desktop\GITHUB\MOBA-proto\.worktrees\m41-cooker-current` | Existing implementation owner; read-only |
| Design branch | `lane/moba-proto-design/20260826` | This packet only |

The accepted M4.1 commit is one clean descendant of `origin/main`; its merge
base with `origin/main` is `e4b3369b36267dcef9fdbcebe2f67b227959d3bd`. The
primary checkout at `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA` was dirty and
diverged at intake; it remains untouched. The active M4.1 worktree was clean
and remains untouched.

The design worktree is intentionally located at
`C:\Users\doton\Desktop\GITHUB-ROOT\_worktrees\CHOONZ-MoBA-design-20260826`.
The design PR targets `codex/m4.1-cooker-restart`; this keeps the PR delta from
the accepted design base docs-only. If that remote target is unavailable, stop
and escalate rather than retargeting to `main` and inheriting engine changes.

### 1.2 Current authority map

| Concern | Current authority | Design rule |
|---|---|---|
| Fixed time | `engine/platform` fixed-step seam and `SIM_HZ` | Simulation is exactly 30 Hz; platform owns accumulator and alpha |
| Simulation state | `engine/sim` `SimWorld` and ECS pools | Simulation owns authoritative state; presentation never receives `SimWorld` |
| Numeric state | `engine/math` fixed-point types | Q16.16 in simulation; int64 intermediates; no float or libm in simulation |
| Entity identity | `engine/core` generational handles | One 32-bit handle, 18-bit index, 14-bit generation; generation zero is invalid |
| Randomness | `SimWorld` PCG32 state | PCG32 is hashed and replayed; presentation/tools use separate RNGs |
| Commands | `SimCommandBuffer` and `sim_validate_commands` | Inputs are intent only; server/replay validates before mutation |
| Events | Double-buffered `engine/sim` event queue | Append order is authoritative; overflow is a deterministic failure, never a drop |
| Replay/hash | Replay v1, explicit LE codec, FNV canonical state hash | Live and replay consume the same ordered command stream and logic hash |
| Assets | M4.1 `engine/assets` registry and `.mba` loader | Runtime reads baked `.mba`; malformed assets fail closed |
| Content build | M4.1 `tools/cooker` | Current `.mba` v1 remains one RGBA8 texture; typed gameplay payloads are future work |
| Presentation | `engine/game` `PresentState` and `RenderSnapshot` | Fixed-to-float conversion happens only in game/presentation |
| Rendering | `engine/render` typed draw interfaces | Renderer receives draw items and frame views, never simulation objects |
| Authority/network | Existing server-authoritative contract | Clients send intent; local prediction/interpolation is bounded presentation behavior |

## 2. Approved decisions and scope fence

### 2.1 Decisions already made

- Retain the custom C++/raw Vulkan engine, its CMake presets, offline SPIR-V
  pipeline, fixed-tick determinism, replay compatibility, and ownership graph.
- Use M4.1 commit `6571ee40...` as the accepted design base.
- Use the roadmap-baseline map model and a single lane for the first playable
  fixture.
- Build local deterministic simulation and replay parity first; make all state
  and command contracts network-ready before implementing transport.
- Keep gameplay content data-driven. Numeric tuning belongs in authored data,
  not C++ control flow.

### 2.2 Vertical-slice boundary

The first fixture has two teams (`0` and `1`), one lane (`0`), one mirrored
hero definition instantiated once for each team, deterministic lane creep
waves, one tower and one core owned by each team, and a core-destruction win
condition. The hero has a basic attack and three representative actions:

1. `projectile_damage`: point-target or line-resolved damage projectile;
2. `self_heal`: self-targeted health restoration;
3. `area_slow`: area effect that applies a fixed-duration movement modifier.

Every value is authored data measured in fixed-point units or integer ticks.
The fixture records gold and XP awards from kills and objectives. It has no
item shop, item stat modifiers, neutral objective, or fog-of-war implementation;
gold and XP are observable ledgers with no spend or level-up sink in this
slice. A harness timeout is a test failure, not an in-game draw rule.

### 2.3 Held items and escalation triggers

The following are non-blocking data or later-phase decisions:

- exact map dimensions, art scale, lane cell coordinates, and balance numbers;
- item shop, neutral objectives, and full fog-of-war rules;
- final typed `.mba` payload encoding and GUI editor implementation;
- transport framing, reliability, and prediction details beyond the existing
  server-authoritative contract.

Escalate instead of guessing if the accepted base moves, a product rule changes,
the custom engine is challenged, or another lane claims one of the write fences.

### 2.4 Explicit out of scope

This lane does not edit `engine/`, `game/`, `tools/`, `tests/`, `assets/`, CMake,
shader sources, generated output, or existing phase files. It does not implement
gameplay, networking, rendering, content formats, an editor, monetization,
live services, or deployment. The only writes are the three new design
artifacts under `docs/` named by the invocation.

## 3. Contract model

The following is a documentation contract, not source code. The types use
fixed-width names and explicit units so a later implementation can copy them
without making a serialization or ownership decision.

### 3.1 Shared scalar rules

| Type | Representation | Rule |
|---|---|---|
| `Tick` | `uint32` | 30 Hz simulation tick; commands name the tick they affect |
| `AssetId` | `uint64` | FNV-1a/64 of normalized path or authored stable ID; never pointer/order-derived |
| `EntityId` | `uint32` | 18-bit pool index plus 14-bit generation; zero/invalid generation is rejected |
| `Fix` | `int32` | Q16.16 authoritative coordinate, range, speed, and modifier |
| `FixWide` | `int64` | Intermediate multiply, divide, and accumulated damage arithmetic |
| `Gold`, `Xp` | `uint32` | Integer ledgers; checked overflow is a deterministic fault |
| `Cooldown` | `uint32` | Remaining ticks, never seconds or wall-clock duration |
| `Enum` | fixed-width unsigned integer | Values are explicit and versioned; unknown values fail closed |

No authoritative contract contains pointers, padding-dependent bytes, hash-map
iteration order, wall-clock values, platform float values, or implicit string
serialization. Arrays have bounded counts and explicit count fields.

### 3.2 Match, team, spawn, and map data

The canonical serialized field order is the order shown below. Arrays are
serialized in ascending authored index; maps and sets are sorted by stable ID
before serialization.

```text
MatchDef {
  uint32 schema_version;
  AssetId mode_id;
  AssetId map_id;
  uint16 sim_hz;                 // must equal 30
  uint8 team_count;              // must equal 2 for this fixture
  uint8 lane_count;              // must equal 1 for this fixture
  uint32 max_match_ticks;        // zero means no game timeout
  uint16 team_def_count;
  uint16 spawn_def_count;
  uint16 objective_def_count;
  uint16 economy_rule_count;
  TeamDef teams[team_def_count];
  SpawnDef spawns[spawn_def_count];
  ObjectiveDef objectives[objective_def_count];
  EconomyRule economy[economy_rule_count];
}

TeamDef {
  uint8 team_id;
  uint8 lane_id;
  uint8 hero_slot_count;
  uint8 reserved;
  AssetId spawn_set_id;
  AssetId hero_def_id;
  AssetId tower_objective_id;
  AssetId core_objective_id;
}

SpawnDef {
  AssetId spawn_id;
  uint8 team_id;
  uint8 lane_id;
  uint16 cell_index;
  int32 position_x_q16;
  int32 position_y_q16;
  int32 facing_q16;
}

MapGrid {
  uint32 schema_version;
  AssetId map_id;
  uint16 width_cells;
  uint16 height_cells;
  int32 cell_size_q16;
  uint32 cell_count;
  MapCell cells[cell_count];  // row-major, y then x
  uint16 lane_def_count;
  LaneDef lanes[lane_def_count];
}

MapCell {
  uint8 flags;                 // walkable, blocked, lane, spawn, objective
  uint8 movement_cost;
  int16 height_cell;
}

LaneDef {
  uint8 lane_id;
  uint8 waypoint_count;
  uint16 wave_interval_ticks;
  uint32 first_wave_tick;
  AssetId creep_archetype_id;
  uint16 waypoints[waypoint_count];
}
```

The map remains data-driven. Exact dimensions and authored cell coordinates are
held, but the schema, row-major ordering, cell units, waypoint ownership, and
spawn references are fixed. Navigation implementation is limited to integer
grid state, fixed-point movement, deterministic neighbor ordering, and bounded
flow-field/A* work; it must not introduce floating-point simulation state.

### 3.3 Hero, action, effect, objective, and economy data

```text
HeroDef {
  uint32 schema_version;
  AssetId hero_def_id;
  int32 max_health;
  int32 move_speed_q16;
  int32 attack_range_q16;
  uint16 action_count;          // three active actions in the fixture
  uint16 reserved;
  ActionDef actions[action_count];
}

ActionDef {
  AssetId action_id;
  uint8 slot;                   // 0, 1, 2 for the fixture actions
  uint8 target_mode;            // point, entity, self, area
  uint16 effect_count;
  uint32 cooldown_ticks;
  uint32 cast_time_ticks;
  uint32 resource_cost;
  int32 range_q16;
  int32 projectile_speed_q16;
  EffectDef effects[effect_count];
}

EffectDef {
  uint8 effect_type;            // projectile_damage, self_heal, area_slow
  uint8 damage_type;            // reserved for the single unified pipeline
  uint16 duration_ticks;
  int32 magnitude;
  int32 radius_q16;
  int32 scalar_q16;
}

ObjectiveDef {
  AssetId objective_id;
  uint8 owner_team_id;
  uint8 objective_kind;         // tower or core
  uint16 reserved;
  int32 max_health;
  int32 armor;
  uint8 target_policy;
  uint8 reserved2[3];
}

EconomyRule {
  uint8 award_kind;              // gold or xp
  uint8 source_kind;             // hero kill or objective damage/destruction
  uint16 reserved;
  uint32 award_amount;
}
```

All effects enter one damage/status pipeline: validate source and target,
resolve base magnitude, apply data-defined modifiers, apply mitigation or
duration rules, append an event, and commit the resulting state in deterministic
order. An ability may not mutate health, cooldown, or resources through a
special path that bypasses this pipeline.

### 3.4 Commands, rejection, and events

```text
Command {
  Tick tick;
  uint8 player_id;
  uint8 command_kind;            // move, basic_attack, use_action
  uint16 sequence;
  EntityId actor;
  AssetId action_id;
  EntityId target;
  int32 point_x_q16;
  int32 point_y_q16;
}

CommandReject {
  Tick tick;
  uint8 player_id;
  uint8 reason;                  // malformed, wrong_tick, stale_entity,
                                  // unauthorized_actor, match_over,
                                  // cooldown, range, capacity
  uint16 sequence;
  EntityId actor;
}

SimEvent {
  Tick tick;
  uint16 event_kind;             // damage, death, objective, economy, match
  uint16 payload_size;
  uint32 append_ordinal;
  byte payload[payload_size];   // fixed-size variant selected by event_kind
}
```

Commands are intent, never direct state writes. For a single tick, accepted
commands are ordered by `(tick, player_id, sequence, actor.index, command_kind)`
after bounds and ownership validation. Duplicate `(player_id, sequence)` values
are rejected. Late network commands are rejected; replay commands must match the
recorded tick. The existing stale-target atomic reject behavior remains valid;
per-command rejection reasons are the M6.0 extension.

The existing 12-byte `DamageEvent` remains the minimum damage event payload.
Event append order is part of the replay/hash contract. If an event buffer is
full, the source operation fails and the tick is not published as successful;
the implementation must never drop the oldest or newest event.

### 3.5 Simulation, replay, snapshots, and presentation

`SimWorld` remains the sole owner of authoritative state. A logical public
snapshot contains only:

```text
PublicSnapshot {
  Tick tick;
  uint64 sim_hash;
  uint8 match_state;
  uint8 team_count;
  uint16 entity_count;
  PublicEntity entities[entity_count];
  PlayerLedger ledgers[visible_player_count];
}
```

`PublicEntity` is filtered by server interest/visibility before transport. It
contains stable entity identity, team, position, health visibility, and public
action state only. It never contains hidden map cells, hidden health, private
cooldowns, or server-only RNG state.

The existing replay v1 format and explicit little-endian codec are reused. A
logic behavior change requires a `SIM_LOGIC_HASH` change; a data-only fixture
change changes the recorded data identity but not the codec rules. Replay
loading fails before simulation on bad magic, version, logic hash, tick, player,
command count, malformed command, overflow, or trailing bytes.

`RenderSnapshot` remains a local presentation projection with `EntityId`, fixed
positions, facing, and tick. `PresentState` owns previous/current snapshots and
interpolation alpha. Camera, selection, and HUD systems read presentation/public
state and emit `Command` intent; they never inspect or mutate `SimWorld`.

### 3.6 Cooker and editor seam

M4.1 `.mba` v1 remains a 32-byte explicit little-endian header with version 1,
type tag, asset ID, zero flags/reserved fields, and one RGBA8 texture payload.
`asset_load` is the only runtime baked-asset path. PNG, glTF, mips, hot reload,
packing, incremental cooking, and typed gameplay payloads are not added here.

The future content lane must add typed payloads as versioned, bounded, explicit
little-endian records whose IDs are stable across recook. Its authoring tool
surface is a deterministic offline pipeline:

1. parse source data into POD records;
2. validate references, counts, ranges, and enum versions;
3. sort by stable ID;
4. emit bytes through one checked writer;
5. inspect and hash the result;
6. reject malformed or duplicate inputs without partial output.

The editor requirement for this phase is therefore a documented schema,
validator, fixture set, and inspect/diff command contract. A GUI editor is
explicitly a later lane.

## 4. Fixed-tick behavior and data flow

Every live and replay tick follows the same ordered schedule:

| Order | System | Required behavior |
|---:|---|---|
| 1 | Ingest | Read only commands assigned to the current tick; never read wall clock |
| 2 | Validate/order | Bounds, ownership, stale handles, duplicate sequence, and deterministic sort |
| 3 | Apply commands | Move, attack, or action intent changes only validated components |
| 4 | Movement/navigation | Resolve integer grid movement and lane waypoints with fixed-point arithmetic |
| 5 | Target/projectile/status | Advance bounded projectile/status state in stable entity order |
| 6 | Combat/events | Resolve unified damage/effect pipeline and append events in order |
| 7 | Death/objective/economy | Apply deaths, tower/core state, victory, gold, and XP ledger events |
| 8 | Cooldown/maintenance | Tick cooldowns and other integer-duration state |
| 9 | RNG tail | Execute unconditional `sys_rng_advance`; new draws are scheduled before this tail |
| 10 | Hash/publish | Hash canonical post-state, publish events/snapshot only on successful tick |
| 11 | Destroy flush | Flush pending destruction after all readers have completed |

The order is a contract. No conditional RNG draw is permitted, no system may
iterate an unordered container, and no presentation callback may run inside the
authoritative schedule. The existing `sys_apply_commands`, `sys_movement`,
`sys_combat_resolve`, `sys_cooldown_tick`, `sys_rng_advance`, and
`sys_flush_destroy` seams are extended in place by future implementation lanes.

Live flow is `input -> intent Command -> server validation -> SimWorld tick ->
public filtered snapshot/events -> PresentState -> DrawItem`. Replay flow is
`Replay v1 -> the same CommandBuffer -> the same SimWorld tick -> hash/event
oracle`. These flows must produce identical authoritative results for identical
inputs.

## 5. Failure states and safe behavior

| Failure | Trigger | Required disposition |
|---|---|---|
| `moba-base-ambiguous` | Main and M4.1 no longer resolve to the accepted base | Stop before branch/write; escalate to owner/Architect |
| `foreign-engine-assumption` | A design or implementation slice requires an external/replacement engine | Reject the slice; retain custom-engine seams |
| `dirty-owner-worktree` | Active M4.1 worktree changes during this lane | Stop and report the conflict; never repair or reset it |
| `docs-fence-breach` | Any proposed write leaves the three new docs artifacts | Stop, restore only with explicit owner direction, and report |
| `invalid-schema` | Unknown version/enum, duplicate ID, bad count, bad reference, or range overflow | Reject the asset/match before publication; no partial state |
| `stale-entity` | Generation or ownership does not match current state | Reject the command atomically and emit a rejection reason |
| `event-overflow` | Event buffer cannot append a required event | Reject the source operation/tick; never drop an event |
| `replay-mismatch` | Header, command, event, or state hash differs | Stop replay at the first mismatch and report tick/field |
| `snapshot-visibility-breach` | Filter would expose hidden/private state | Reject the snapshot build and fail the acceptance test |
| `cooker-nondeterminism` | Same valid source emits different bytes or IDs | Fail the cooker check; do not publish output |

## 6. Dependency DAG and future implementation slices

Each slice below is one claim with one primary writer. Slices that touch the
same `engine/sim/` fence are serial unless their file-level claim is explicitly
disjoint.

| Slice | Single claim | Depends on | Builds on M4.1 | Future write fence |
|---|---|---|:---:|---|
| `m5-map-navigation` | Integer map grid, lane waypoints, passability, spawns, and deterministic navigation | M3.4 sim/hash/replay contracts | Yes, for baked map identity/loading | `engine/sim/`, `tests/sim/` map-owned files |
| `m5-command-replay` | Live/replay command parity, ordering, stale-handle and capacity rejection | M3.4 replay/hash | No direct runtime dependency | `engine/sim/`, `tests/sim/` command/replay-owned files |
| `content-typed-payloads` | Validated hero/action/map/economy payloads and deterministic cooked bytes | Accepted M4.1 `.mba` boundary | Yes; only after M4.1 implementation acceptance | `engine/assets/`, `tools/cooker/`, `tests/assets/` |
| `m5-hero-combat` | Basic attack, three effects, projectile/status state, cooldowns, and damage events | Map, command, typed payload slices | Yes for stable content identity | `engine/sim/`, `tests/sim/` combat-owned files |
| `m5-lane-objectives` | Creep waves, tower/core state machines, victory, gold, and XP ledger | Map, commands, hero/combat | Yes for content-loaded definitions | `engine/sim/`, `tests/sim/` objective-owned files |
| `m6-authority-replication` | Server command intake, public snapshots, filtering, and replay diagnostics | Command, combat, lane state | No direct runtime dependency | `engine/net/`, `tests/net/` |
| `m7-presentation` | Camera, selection, input translation, interpolation, HUD/minimap seams | Render snapshot and command contracts | Yes for presentation assets | `engine/game/`, `engine/render/`, `tests/game/` |

The minimum vertical path is `m5-map-navigation -> m5-command-replay ->
content-typed-payloads -> m5-hero-combat -> m5-lane-objectives`. The first
playable local slice closes after the fifth slice. Replication and presentation
then consume the same contracts without moving authority into the renderer.

## 7. Acceptance tests and hardware gates

### 7.1 Design-lane checks

The design lane must pass the following observable checks before delivery:

- The branch parent is exactly `6571ee40adfd7e99a49564a7c0d21d4702ee80c8` and
  the recorded merge-base is exactly `e4b3369b36267dcef9fdbcebe2f67b227959d3bd`.
- `git diff --name-only 6571ee40...HEAD` lists only the new slate, manifest,
  and receipt under `docs/`.
- `git diff --check` exits zero.
- The ARC manifest parses and passes the repository ARC compiler/checker.
- Repository-native documentation/schema checks pass where available.
- The packet contains no implementation writes and no external engine dependency.
- The receipt records all executed checks, unavailable checks, source pins,
  accepted base, and unresolved held items.

### 7.2 Future implementation acceptance

The implementation lanes must add executable tests for these scenarios:

1. Two identical command streams produce identical final state hash, event
   order, public snapshot, and replay result in Debug and Release.
2. Commands with bad bounds, wrong tick, duplicate sequence, stale generation,
   wrong owner, cooldown, range, match-over, or capacity are rejected without
   mutating authoritative state.
3. A full event buffer fails the source operation and never silently drops an
   event.
4. Conditional RNG draws are detected; unconditional RNG advancement and
   replay parity remain stable after adding a new action.
5. The mirrored hero can use projectile damage, self-heal, and area slow through
   the unified effect pipeline; cooldown and resource state use integer ticks.
6. Creep waves reach the opposing tower/core deterministically; core destruction
   ends the match and emits objective/economy/match events in stable order.
7. Server snapshots exclude hidden/private state and clients cannot authoritatively
   mutate `SimWorld` through presentation or transport input.
8. Valid content recooks to byte-identical `.mba` output; malformed, duplicate,
   unknown-version, and bad-reference data fails closed.
9. Replay rejects bad magic/version/logic hash/tick/player/count/command,
   overflow, and trailing bytes at the first invalid boundary.

### 7.3 Existing and future hardware gates

The packet cites the recorded M3.4/M4.0 Debug, RelWithDebInfo, Release, ASan,
and RTX 4070 Ti evidence, including the established deterministic oracle and
logic hash. This docs-only lane does not claim a fresh M4.1 CMake/CTest or
hardware run. Every implementation slice must later run the repository's
configured `cmake --preset dev`, Debug build, CTest suite, CI warning-as-error
configuration, offline SPIR-V checks, and applicable hardware validation from a
clean exact head.

## 8. Delivery instructions

1. Work only in the dedicated design worktree and read repository guidance
   before any write.
2. Add only the new slate, ARC manifest, and receipt under `docs/`.
3. Run the design-lane checks and inspect the complete diff.
4. Commit with `docs: define M4.1 one-lane MOBA design packet`.
5. Push `lane/moba-proto-design/20260826` and open a PR against
   `codex/m4.1-cooker-restart`; never merge it.
6. Close the receipt with the actual head commit, PR URL, check results, and
   unresolved gates. Do not claim tests or hardware runs that were not executed.
7. Stop and escalate on base drift, dirty owner worktree, fence breach, or any
   request to implement engine behavior in this lane.

## 9. Source and acceptance references

- `C:\Users\doton\Desktop\GITHUB-ROOT\_worktrees\GROM\GromAtlas\pr139-moba-proto-design-20260826\_INGRESS\creative-tower-execution-program-20260826\INVOCATION-MOBA-PROTO-DESIGN.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\AGENTS.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\README.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\ARCHITECTURE.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\ROADMAP.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\arc-m3.4-manifest.json`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\slate-moba-phase3-m3.4.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\slate-moba-phase4-m4.0.md`
- `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA\docs\testing-hardware.md`
