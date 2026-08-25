# Unit and ability schema — the shape content fills

- **Status:** Draft spec for M5.3 · 2026-08-25
- **Implements:** `ROADMAP.md` M5.3 · **Resolves through:** `01-combat-damage-pipeline.md`
- **Constrained by:** ADR-0002 (Q16.16), ADR-0003 (handle ABI), ADR-0010 (AssetId), and the
  `SIM_MAX_UNITS = 64` replay-codec cap (`ROADMAP.md` M5.2, G23).

> **No content is invented here.** This file defines *fields, types, units, ranges and
> conversions* — the shape. Every concrete number that appears is labelled **illustrative** and is
> there only to show the unit and magnitude a field expects. The role list, the champion roster,
> and all balance values are owner-authored and are listed as open items in §10.

---

## 1. The rule this schema exists to enforce

> **Adding the twentieth unit must be filling in a table, not writing C++ or inventing structure.**

If a new unit needs a new field, a new state, or a new special case in a system, the schema failed
and the fix belongs *here*, not in that unit. That is the test to apply to every future addition.

Corollary: **balance values live in data, never in C++.** Tuning is a re-cook, not a recompile.

---

## 2. Anatomy of a unit

A unit definition is four blocks. Runtime storage stays in the existing typed sparse-set SoA
pools — this is the *authoring* record and its cooked form, not a runtime `struct` layout.

```
UnitDef
  identity   unit_id, name_key, role, collision_radius, selection_flags
  stats      the per-level baked table (§4)  <- the balance surface
  states     which of the engine's states this unit uses (§6)
  kit        ordered ability slots (§7)
```

### 2.1 Identity

| Field | Type | Notes |
|---|---|---|
| `unit_id` | `AssetId` (u64) | ADR-0010 normalized-path FNV-1a/64. Cook-time collision diagnostics already exist from M4.0 — reuse them, do not add a second id scheme. |
| `name_key` | `AssetId` | Points at a localization string, **never a display literal.** Costs nothing now; retrofitting is expensive. |
| `role` | `u8` enum | **Owner-ratified 2026-08-25:** `ROLE_BRUISER=0`, `ROLE_CARRY=1`, `ROLE_BURST=2`, `ROLE_SUPPORT=3` (`04-` §2). Values are hashed state — never renumber. The schema may not mint further roles; extending the list is an owner decision. Role drives AI target priority, bot behaviour, and the champion budget. |
| `collision_radius` | `fix` | World units. Consumed by the M5.1 separation pass. |
| `flags` | `u32` | Bitfield: `IS_HERO`, `IS_STRUCTURE`, `IS_MINION`, `TARGETABLE`, `BLOCKS_MOVEMENT`, `GRANTS_VISION`, `SELECTABLE`. Bit meanings are hashed state — never renumber. |

---

## 3. Where each stat lives, and in what unit

Every field below is either `fix` (Q16.16) or an integer **tick count**, per `01-` §6 Law 1.
Nothing is stored in seconds.

| Stat | Storage | Unit | Notes |
|---|---|---|---|
| `hp_max` | `fix` | HP | Must satisfy `HP_MAX` (`01-` §7). |
| `hp_regen` | `fix` | HP **per tick** | Cook-time converted from HP/sec. |
| `energy_max` | `fix` | energy | **Ratified 2026-08-25: energy** — a small pool with fast regen. It caps *burst rate* rather than total output, so it gates ability chaining without creating out-of-mana dead time in a 12–18 minute match. |
| `energy_regen` | `fix` | per tick | Cook-time converted from energy/sec. Fast regen means this is a large per-tick value relative to the pool — see §3.4. |
| `move_speed` | `fix` | world units **per tick** | Cook-time converted from units/sec. |
| `attack_damage` | `fix` | damage | Feeds stage 1 of the damage pipeline. |
| `attack_speed` | `fix` | attacks **per second** | **Stays per-second** — it is consumed by the `01-` §6 Law-2 accumulator, which requires the per-second rate. This is the one deliberate exception. |
| `attack_range` | `fix` | world units | Compared squared; never square-rooted in sim. |
| `attack_windup` | `u16` | ticks | Point in the swing where the damage event is queued. |
| `armor` | `fix` | resist | Clamped to `[RESIST_MIN, RESIST_MAX]` (`01-` §4.4). |
| `magic_resist` | `fix` | resist | Same clamp. |
| `vision_radius` | `u16` | **grid cells** | Integer — M5.6 line-of-sight is integer by design. |

**Four conventions worth stating once:**

1. **Ranges are compared squared.** `dist_sq <= range*range` in Q16.16 avoids `fix_sqrt` on the
   hottest path in the sim. Watch the magnitude: a range of 12 units squares to 144, far inside
   the `fix` ceiling, but a range beyond ~180 units would overflow the squared comparison — assert
   `attack_range < 180` at cook time.
2. **Per-tick conversion happens in the cooker, once.** Designers author in seconds and units/sec;
   the cooker emits per-tick values. `01-` §6 explains why the sim must never see `SIM_DT_FIXED`.
   Conversion cost is a one-time Q16.16 quantization of about `1.5e-5` per field — three orders of
   magnitude below any value a designer will type.
3. **`attack_speed` is the exception** because Law 2's accumulator consumes the rate directly and
   is drift-free precisely *because* it does not pre-divide. Flag it in the cooker so nobody
   "fixes" it later.
4. **Energy is a small pool with fast regen, and that ordering matters.** A pool of ~100 with
   regen around 10/sec refills in ~10 seconds, so `energy_regen` per tick is roughly `0.333` — a
   value where Q16.16's `1.5e-5` resolution is four orders of magnitude finer than the quantity,
   so cook-time conversion loses nothing. Design consequence: **cost balance is about how many
   abilities chain back-to-back, not about total abilities cast per match.** Two abilities that
   each cost 40 % of the pool is a hard two-then-wait; two at 30 % is a comfortable three-chain.
   That is the lever — tune costs as a fraction of `energy_max`, not as absolute numbers.

---

## 4. Level scaling — bake the table, do not evaluate a curve

The obvious design is a growth coefficient plus a runtime formula. **Do not do that.** Instead:

> **The cooker emits a flat per-level array of final values. The sim performs no scaling math at
> all — it indexes.**

```
stats[MAX_LEVEL][STAT_COUNT]     /* fix, cooked, immutable */
hp_max_at_level_7 = def->stats[6][STAT_HP_MAX];
```

Four reasons this is the right call here specifically:

1. **The curve never enters the sim**, so it can be anything — linear, quadratic ramp, hand-tuned
   per level, a spreadsheet export. Designers get full freedom and the sim gets zero risk.
2. **Zero runtime rounding ambiguity.** A curve evaluated per tick is a per-tick rounding decision
   that must be bit-identical across MSVC, clang-cl, Debug and Release — one more thing the
   determinism gate has to cover. A lookup is exact by construction.
3. **Offline float is legal.** The cooker is not sim code and is not bound by the float ban, so
   the curve can be authored in ordinary floating-point maths and quantized once at bake time.
4. **It is free.** At the ratified `MAX_LEVEL = 8` (§7), 8 levels × ~12 stats × 4 bytes ≈ **384
   bytes per unit** — about **2.3 KB for the whole six-champion roster**. There is no cost to
   trade against.

The same treatment applies to per-rank ability values (damage, cooldown, cost): **bake the rank
table, index it.**

---

## 5. The stat-modifier surface

Runtime stats are `base_from_level + Σ(modifiers)`. Per `01-` §5, modifiers of the same kind
**sum as integers** and resolve to one multiplier, so the result is order-independent and needs no
tie-break rule.

```
effective_stat = fix_mul(base[level][stat] + flat_sum[stat], FIX_ONE + pct_sum[stat])
```

`flat_sum` and `pct_sum` are recomputed only when a modifier is added or removed — dirty-flagged,
not per tick. Sources: items, ability ranks, statuses, auras. All four use the same two
accumulators; none of them gets a bespoke path.

---

## 6. Unit states

The state set and its transitions are **engine-owned**. A unit definition declares only which
states it *uses* — it may not add one.

| State | Meaning | Blocks |
|---|---|---|
| `IDLE` | no order | — |
| `MOVING` | following a path or flow field | — |
| `ATTACK_WINDUP` | swing started, damage not yet queued | movement |
| `CASTING` | fixed-duration cast | movement, attack |
| `CHANNELING` | interruptible sustained cast | movement, attack |
| `STUNNED` | crowd control | orders, movement, attack, cast |
| `DEAD` | awaiting respawn | everything |

Two rules that keep this deterministic and debuggable:

- **Transitions are a table, not scattered `if`s.** A single `state_can_transition[from][to]`
  matrix, checked in one place. Illegal transitions assert in Debug.
- **Interrupt policy is per state, declared once** — what cancels a channel, what a stun does to a
  windup. Encoding it per ability is how two abilities end up disagreeing about the same rule.

---

## 7. The kit

```
KitSlot { ability_id: AssetId, slot: u8, unlock_level: u8, max_rank: u8 }
```

Slots are an **ordered array** — order is hashed state, so it is stable across a replay. Slot
count is a schema constant (`KIT_SLOT_COUNT`), not per unit; a unit with fewer abilities leaves
slots empty rather than varying its record size.

### 7.1 Ratified constants (2026-08-25)

```c
#define KIT_SLOT_COUNT   3    /* two basics + one ultimate, on top of the basic attack */
#define MAX_LEVEL        8
#define MAX_RANK_BASIC   3
#define MAX_RANK_ULT     2
```

**The point budget closes exactly.** One rank point per level gives 8 points; total available
ranks are `2 basics × 3 + 1 ultimate × 2 = 8`. A champion is therefore fully ranked at exactly
level 8 with no leftover points and no unreachable rank — which removes a whole class of "did I
spend these right?" balance noise, and makes the level curve trivially readable.

**Why 3 slots and not 4.** Across a 4–6 champion roster this is 12–18 abilities instead of
16–24 — roughly **25 % less ability content** to author, VFX, balance and bug-fix. At this scale
content is the dominant cost (`04-` §5), and every ability is a permanent maintenance obligation.
Three slots still gives each champion a real kit identity: an engage or poke, a signature, and an
ultimate.

**The unlock schedule is content**, not schema — which level the ultimate becomes available at,
and whether basics rank freely, are tuning values in the kit table. The schema only fixes the
budget above.

**Interaction with `SIM_MAX_UNITS = 64` (finding F2).** The cap is on *commanded units per tick*,
not on entities. Heroes are commanded; minions, projectiles and structures are AI-driven and do
not consume the budget. Under Shape B (`00-` §4) the ceiling is ~6 commanded units — no pressure
whatsoever. Under Shape A with player-controlled pets or summons it could bind. **Confirm against
the chosen shape before M5.2**, because widening it afterwards is a deliberate replay-format and
logic-hash break.

---

## 8. The ability definition

The **core record is identical under both sides of decision F1** (fixed effect vocabulary vs. the
D-16 Lua VM). Only the `behaviour` field differs.

> **F1 RESOLVED 2026-08-25 → Option 1, the fixed effect vocabulary.** The instruction set is
> specified in [`03-effect-vocabulary.md`](03-effect-vocabulary.md). D-16 is deferred rather than
> overturned, and `03-` §2 keeps the migration a per-ability list swap. §8.1 below is retained as
> the reasoning behind the call.

```
AbilityDef
  ability_id      AssetId
  name_key        AssetId
  targeting       enum { SELF, UNIT, POINT, DIRECTION, NONE }
  range           fix        (world units; squared-compare, see 3.1)
  cost[rank]      fix        (baked per rank)
  cooldown[rank]  u16        (TICKS, baked per rank)
  cast_time       u16        (ticks)
  channel_time    u16        (ticks; 0 = not a channel)
  flags           u32        { INTERRUPTIBLE, CASTABLE_WHILE_MOVING, IGNORES_CC, ... }
  behaviour       <-- the F1 fork
```

### 8.1 The fork

- **Option 1 — fixed effect vocabulary** (`ROADMAP.md` M5.3): `behaviour` is an ordered
  `Effect[]` drawn from a closed set — `DAMAGE`, `HEAL`, `SPAWN_PROJECTILE`, `APPLY_STATUS`,
  `DASH`, `AOE_QUERY`. Simpler, fully native, no VM to sandbox or fuel-meter; every new ability
  archetype needs a vocabulary extension and therefore an engine change.
- **Option 2 — D-16 Lua VM**: `behaviour` is a cooked-bytecode `AssetId`, and the script fills
  four hooks — `onCast` → `onTick` → `onHit` → `onExpire`. Far more expressive; costs a sandbox, a
  deterministic fuel meter, a fixed-point-only API surface, and per-tick checksum coverage of
  script-touched state.

**What holds either way** — and this is why the fork does not block anything downstream:

1. **`onHit` (or the `DAMAGE` effect) only ever *queues* a damage command.** Native systems apply
   it, through the one pipeline in `01-`. No authored content writes HP.
2. **Costs and cooldowns are validated from the table**, never from the behaviour. Behaviour
   describes *what happens*; the table describes *what it costs*.
3. **Every duration in the record is already in ticks**, so neither option can reintroduce the F3
   seconds/ticks divergence.
4. **AoE and multi-target queries return victims sorted by ascending `EntityId`** before any
   effect is applied — spatial-hash traversal order is not a stable order.

**Recommendation, for the F1 decision.** Under launch Shape B (4–6 champions), Option 1's real
cost — an engine change per new ability archetype — is bounded and small, while Option 2's costs
(sandbox, fuel meter, bytecode cooking, signed packfiles, extending the determinism gate to cover
script state) are close to fixed regardless of roster size. **Option 1 is the better fit for a
small roster; Option 2 pays for itself as the roster grows.** D-16 is marked
`reversibility: medium`, and `AbilityDef` above is the seam that keeps it reversible — so starting
with Option 1 does not foreclose Option 2. This is an owner call; the analysis is offered, not the
decision.

---

## 9. The cook-time contract

The cooker is the only place where designer units become sim units, so it is the right place to
fail loudly.

| Check | Failure mode it prevents |
|---|---|
| seconds → ticks for every duration | the `01-` §6 F3 divergence |
| units/sec → units/tick for `move_speed`, regens | same |
| `attack_speed` left per-second, explicitly flagged | someone "fixing" the Law-2 exception later |
| `hp_max <= HP_MAX`, `attack_range < 180` | silent Q16.16 wrap (`01-` §7) |
| `armor`/`mr` within `[RESIST_MIN, RESIST_MAX]` | the `int32` overflow in `01-` §4.4 |
| every `AssetId` resolves; no collisions | reuses the M4.0 collision diagnostics |
| every `kit` slot's `unlock_level <= MAX_LEVEL` | out-of-range table index |
| byte-identical output from repeated cooks | matches the M4.1 reproducibility requirement |

---

## 10. Open items — owner-authored, and what each one blocks

| # | Item | Status | Blocks |
|---|---|---|---|
| 1 | **The role list.** Roles are owner-authored by rule; the schema cannot mint them. | ✅ **RATIFIED 2026-08-25** — `BRUISER` / `CARRY` / `BURST` / `SUPPORT` (`04-` §2) | — unblocked |
| 2 | **The resource kind** | ✅ **RATIFIED — energy** (small pool, fast regen). §3, §3.4 | — unblocked |
| 3 | **`MAX_LEVEL`** | ✅ **RATIFIED — 8.** §7.1 | — unblocked |
| 4 | **`KIT_SLOT_COUNT`** | ✅ **RATIFIED — 3** (two basics + ultimate). §7.1 | — unblocked |
| 5 | **F1 — the ability authoring model.** | ✅ **resolved → fixed effect vocabulary** (`03-`) | — |
| 6 | **The roster** — how many champions, and who they are. | ✅ **unblocked** — budget 4–6 (`04-` §2); roles ratified; fiction deferred but roles are functional, so kits can be authored before names exist | — |

**Every schema-blocking decision is now closed.** The remaining open items are tuning and content
questions, not shape questions: the ability unlock schedule (§7.1), whether critical strikes exist
(`01-` §10), and the negative-resist curve (`01-` §4.2). None of them blocks authoring a champion.
