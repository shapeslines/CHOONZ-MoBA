# The effect vocabulary — abilities as data, no VM

- **Status:** Draft spec for M5.3 · 2026-08-25
- **Decision recorded:** open item **F1 is resolved in favour of the fixed effect vocabulary**
  (`ROADMAP.md` M5.3). Decision-index **D-16 (Lua 5.4 VM) is deferred, not overturned** — §2 keeps
  it reversible by construction, which is what its `reversibility: medium` grade asks for.
- **Resolves through:** `01-combat-damage-pipeline.md` · **Fills:** `02-unit-ability-schema.md` §8.1

---

## 1. What this is

A closed instruction set. An ability is an **ordered list of `EffectOp`s** drawn from it, with all
parameters in cooked per-rank tables. Adding an ability writes no C++. Adding an *archetype* the
vocabulary cannot express extends the vocabulary — deliberately, and here.

The set below is deliberately small. Under the chosen launch shape (4–6 champions, `04-` §2) a
small orthogonal set covers the design space, and every opcode is a permanent maintenance and
determinism obligation. **Resist growing it before an actual ability needs it.**

---

## 2. The structuring principle: mirror the D-16 hooks

The vocabulary is organised into **four lists per ability**, named to match the D-16 lifecycle
hooks exactly:

| List | When it runs | D-16 equivalent |
|---|---|---|
| `on_cast` | once, when the cast completes | `onCast` |
| `on_tick` | every tick during a channel | `onTick` |
| `on_hit` | when a spawned projectile connects | `onHit` |
| `on_expire` | when a channel ends or a projectile dies | `onExpire` |

This is the load-bearing structural choice. It costs nothing now, and it means a later migration
to D-16 is a **per-list swap** — replace one `EffectOp[]` with one script, ability by ability,
with both models coexisting during the transition — rather than a redesign of `AbilityDef`. If the
roster ever outgrows the vocabulary, that path stays open at low cost. That is precisely what
D-16's `reversibility: medium` grade is asking to be preserved.

---

## 3. Selectors and effects are separate

The roadmap's sketch lists `aoe` alongside `damage` and `heal`, which conflates *who* with *what*
and forces every effect to re-run its own query. Split them:

> **Selectors write the target register. Effects read it.**

The **target register** is a fixed-capacity array of `EntityId` (`MAX_TARGETS = 16`), rebuilt by
each selector and consumed by every effect until the next selector. So `SEL_AREA → DAMAGE →
APPLY_STATUS` runs the area query **once** and applies both effects to the same victim set — which
is also the only way to guarantee both hit exactly the same units.

---

## 4. The instruction set

### 4.1 Selectors — write the register

| Op | Params | Notes |
|---|---|---|
| `SEL_CASTER` | — | The casting unit. Always exactly one entity. |
| `SEL_TARGET` | — | The ability's declared target from `targeting` (`02-` §8). Empty for `POINT`/`DIRECTION` abilities. |
| `SEL_AREA` | `anchor`, `shape`, `radius[rank]`, `angle`, `team_mask`, `max_targets` | `anchor` ∈ {caster, target_point, projectile}. `shape` ∈ {circle, cone, rect}. |

**`SEL_AREA` determinism contract** — three rules, all mandatory:

1. Query the spatial hash, then **sort victims by ascending `EntityId`**. Spatial-hash traversal
   order is a memory-layout artifact, not a stable order.
2. **Truncate to `max_targets` after the sort**, never before. Truncating a hash-ordered list is a
   non-deterministic choice of victims; truncating a sorted one is a deterministic rule.
3. Compare distances **squared**, in Q16.16. Never `fix_sqrt` on this path (`02-` §3.1).

### 4.2 Effects — read the register

| Op | Params | Notes |
|---|---|---|
| `DAMAGE` | `base[rank]`, `scale_stat`, `scale_ratio[rank]`, `damage_type` | **Queues** a `DamageEvent` per target. Amount = `base + fix_mul(caster_stat, ratio)`. Never writes HP — `01-` §1. |
| `HEAL` | `base[rank]`, `scale_stat`, `scale_ratio[rank]` | Queues a heal command. Same pipeline discipline; healing is negative-signed damage routed to a type that skips mitigation but not shields-or-clamp. |
| `APPLY_STATUS` | `status_id`, `duration_ticks[rank]`, `stacks` | Queues a status application. Stacking rule comes from the status definition (`01-` §5), never from here. |
| `REMOVE_STATUS` | `category_mask` | Cleanse. Mask over status categories, not individual ids — cleanses should not need editing when a status is added. |
| `SPAWN_PROJECTILE` | `projectile_id`, `speed`, `max_range`, `pierce_count` | Spawns into `ProjectileSoA`. Its `on_hit`/`on_expire` lists come from the projectile definition. |
| `DASH` | `subject`, `distance[rank]`, `speed`, `collision_policy` | `subject` ∈ {caster, targets}. `collision_policy` ∈ {stop_at_wall, stop_at_unit, pass_through}. |
| `MODIFY_ENERGY` | `amount[rank]`, `sign` | Drain/restore **energy** (ratified resource, `02-` §3) beyond the ability's base cost. Express amounts as a fraction of `energy_max` when tuning — `02-` §3.4. |

### 4.3 Control

| Op | Params | Notes |
|---|---|---|
| `DELAY` | `ticks` | Defers the remainder of the list. **At most one per list, and only at top level** — see §6. |

That is **11 opcodes**. Checked against the standard MOBA archetype set:

| Archetype | Composition |
|---|---|
| Skillshot | `SPAWN_PROJECTILE` → (projectile `on_hit`: `SEL_TARGET` → `DAMAGE` → `APPLY_STATUS`) |
| Point-blank AoE | `SEL_AREA(caster)` → `DAMAGE` |
| Targeted nuke | `SEL_TARGET` → `DAMAGE` |
| Ally heal / shield | `SEL_TARGET` → `HEAL` / `APPLY_STATUS` |
| Self-buff | `SEL_CASTER` → `APPLY_STATUS` |
| Gap-closer with impact | `DASH` → `SEL_AREA(caster)` → `DAMAGE` |
| Delayed ground AoE | `DELAY(n)` → `SEL_AREA(target_point)` → `DAMAGE` |
| Channelled drain | `on_tick`: `SEL_TARGET` → `DAMAGE` → `HEAL(caster)` |
| Cleanse | `SEL_CASTER` → `REMOVE_STATUS(mask)` |

**Known not expressible, and deliberately deferred:** chaining/bouncing (`SEL_CHAIN`),
conditional branches (`if target below X% HP`), and per-target scaling. Each is a real archetype
and each is a v2 opcode. None is needed for a 4–6 champion roster, and adding them speculatively
is how a "fixed vocabulary" quietly becomes a badly-designed VM.

---

## 5. Execution semantics

1. Ops execute **in list order**, one at a time, to completion.
2. Effects apply to **every entity in the register, in register order** (ascending `EntityId` by
   §4.1). Two effects after one selector are guaranteed the same victim set.
3. An empty register makes subsequent effects **no-ops, not errors** — a targeted ability whose
   target died mid-cast fizzles rather than asserting.
4. **Nothing mutates another entity directly.** `DAMAGE`, `HEAL` and `APPLY_STATUS` queue commands
   drained by native systems later in the schedule. This is the same command-buffer law D-16 would
   impose on scripts, applied to the vocabulary — which is why the migration in §2 stays cheap.
5. **No RNG in v1.** No opcode draws. If critical strikes are added, the draw happens **inside the
   damage pipeline**, once per `DamageEvent` in its deterministic drain order — never in an
   effect op, where the draw count would depend on register size.

---

## 6. Bounds — the vocabulary's answer to D-16's fuel meter

A VM needs a fuel meter because a script can loop. This vocabulary cannot loop, so bounds are
**static and checked at cook time** — strictly cheaper and strictly stronger:

```c
#define MAX_EFFECTS_PER_LIST   8    /* ops in one on_cast/on_tick/on_hit/on_expire */
#define MAX_TARGETS           16    /* target register capacity                    */
#define MAX_DELAY_PER_LIST     1    /* at most one DELAY, top level only           */
#define MAX_PROJECTILE_DEPTH   1    /* a projectile's on_hit may not SPAWN_PROJECTILE */
```

`MAX_PROJECTILE_DEPTH = 1` is the important one: without it, a projectile spawning a projectile is
an unbounded recursion with no fuel meter to stop it. Enforce at cook time by walking the
projectile graph and rejecting any cycle.

`MAX_DELAY_PER_LIST = 1` keeps deferred execution a flat schedule entry rather than a tree, so a
delayed remainder is one pending record with a fire tick — trivially hashable and replayable.

**Worst-case cost per ability activation is therefore a compile-time constant** — 8 ops × 16
targets = 128 queued commands. That is a number the fixed-capacity event buffer can be sized
against, which a VM's fuel budget can only bound probabilistically.

---

## 7. Cook-time validation

| Check | Prevents |
|---|---|
| every effect op is preceded by a selector in its list | reading a stale or empty register |
| `DELAY` count ≤ 1, top level only | a scheduling tree |
| projectile graph is acyclic, depth ≤ 1 | unbounded recursion |
| ops per list ≤ `MAX_EFFECTS_PER_LIST`; `max_targets` ≤ `MAX_TARGETS` | buffer overrun |
| every `status_id` / `projectile_id` resolves | dangling `AssetId` |
| every `[rank]` array length == the slot's `max_rank` | out-of-range rank index |
| `damage_type` is a declared member of the closed enum | an ability inventing a damage type |
| every duration parameter is in **ticks** | the `01-` §6 seconds/ticks divergence |
| opcode values are never renumbered | replay/hash break |

---

## 8. Definition of Done for M5.3

The roadmap's DoD is *"a few hand-authored abilities (a skillshot, a heal, a slow) work end to
end; cooldowns and status durations tick in fixed-point; determinism self-check green."*
Concretely:

- [ ] The three roadmap abilities are authored **as data only** — the diff that adds them touches
      no `.cpp`. That is the real test of this spec.
- [ ] A fourth ability, composed from ops the first three did not use (`DASH` → `SEL_AREA` →
      `DAMAGE`), also lands with no C++ change.
- [ ] `SEL_AREA` ordering test: the same overlapping-units fixture yields an identical hash across
      two different spatial-hash insertion orders (proves the §4.1 sort).
- [ ] Truncation test: 20 valid targets with `max_targets = 16` selects the 16 **lowest**
      `EntityId`s, deterministically.
- [ ] Fizzle test: target dies mid-cast → remaining effects no-op, no assert, hash stable.
- [ ] Cook-time rejection tests, one per §7 row.
- [ ] The 10 000-tick determinism self-check stays green with abilities firing, Debug and Release.

---

## 9. Deferred, with the reason

| Item | Why not now |
|---|---|
| `SEL_CHAIN` (bounce) | no roster ability needs it; a v2 opcode |
| Conditional ops (`IF_HP_BELOW`) | the point where a vocabulary starts becoming a VM — if two abilities need it, prefer D-16 (§2) over growing branches here |
| Per-target scaling | same |
| Ability RNG | none in v1; if crits arrive they belong in the pipeline (§5.5) |
| D-16 Lua VM | deferred by the F1 decision, kept reversible by §2 |
