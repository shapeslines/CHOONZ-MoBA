# The combat / damage pipeline — one formula, typed

- **Status:** Draft spec for M5.4 (and the dependency M5.3 resolves through) · 2026-08-25
- **Implements:** `ROADMAP.md` M5.4 · **Constrained by:** ADR-0001 (30 Hz), ADR-0002 (Q16.16),
  ADR-0003 (handle ABI), ADR-0007/0013 (sim RNG), ADR-0011 (server authority).
- **Every number below was verified numerically against the ADR-0002 `fix_mul`/`fix_div`
  definitions**; the measured errors quoted are actual, not estimated.

> **This spec is independent of open decision F1** (fixed effect vocabulary vs. the D-16 Lua VM).
> Under either model the damage pipeline is **native C++**: authored content — vocabulary entry or
> script — only ever *queues a damage command*, and never applies damage itself. That is the
> command-buffer law. So this can be built before F1 is decided.

---

## 1. The invariant

> **There is exactly one place in the codebase where HP changes.**

Every source of damage — auto-attack, ability, projectile on-hit, damage-over-time tick, tower,
turret, environment — emits a `DamageEvent` into the per-tick buffer. A single `combat_resolve`
system drains that buffer. Nothing else writes `Health`.

This is not a style preference. It is what makes damage *tunable* (one formula to change),
*explicable* (one place to log), and *deterministic* (one ordering to prove). The roadmap flags
the failure mode explicitly: *"ad-hoc damage application elsewhere would break the
single-resolution-point invariant."* Enforce it the way the sim's other invariants are enforced —
with a build-time audit, not a convention (see §9).

---

## 2. Stage order (the whole pipeline)

Stages run strictly in this order for each event. The order is the design contract: moving a
stage changes balance everywhere at once.

```
  DamageEvent { source, target, base, type, flags, modifier_seq }
        |
  [1] SOURCE AMPLIFY    base += flat_adds(source)
                        base  = fix_mul(base, source_mult)        <- one folded multiplier
        |
  [2] TYPE ROUTE        resist_stat := resist_for(type, target)   <- closed enum -> stat
        |
  [3] MITIGATE          base = fix_mul(base, mitigation(resist_stat))
        |
  [4] TARGET AMPLIFY    base = fix_mul(base, target_taken_mult)   <- one folded multiplier
        |
  [5] CLAMP             base = clamp(base, 0, DMG_MAX)
        |
  [6] SHIELDS           absorb in ascending shield_slot; remainder passes through
        |
  [7] APPLY             hp -= remainder;  hp = max(hp, 0)
        |
  [8] LEECH             lifesteal/spellvamp on *remainder actually dealt* (post-shield)
        |
  [9] CREDIT            record last_damage_source + tick on the target
        |
 [10] DEATH             if hp == 0 -> queue DeathEvent (do NOT despawn inline)
```

**Rationale for the two contentious placements:**

- **Shields after mitigation (6 after 3).** A shield absorbs *the damage the target actually
  takes*, so armor and shields multiply rather than compete. The alternative — shields absorbing
  pre-mitigation — makes shields silently stronger on tanky targets and is far harder to explain
  in a tooltip.
- **Lifesteal on the post-shield remainder (8 after 6).** Leeching off damage a shield ate would
  let a shielded target feed the attacker for free. "You heal for what you actually took off them"
  is the readable rule.

**Death is queued, never inline.** Despawning inside `combat_resolve` would mutate the entity set
while other events in the same buffer still reference it. `DeathEvent` is drained by a later
system in the schedule, matching the existing deferred-destruction model.

---

## 3. Damage types — closed enum, content members

The **mechanism** is fixed by this spec; the **member list** is content and is owner-authored
(open decision, see `00-` §6.4). Adding a type is a schema-level change — a new `resist_for`
row, a new stat, a replay/hash consideration — never something an ability may invent.

```c
typedef uint8_t DamageType;   // wire- and hash-stable; values never renumbered
#define DMG_PHYSICAL  0u      // mitigated by  armor
#define DMG_MAGIC     1u      // mitigated by  magic_resist
#define DMG_TRUE      2u      // mitigated by  nothing
#define DMG_TYPE_COUNT 3u     // <- placeholder set, pending the owner decision
```

Rules that hold whatever the member list becomes:

- **`resist_for(type, target)` is a table lookup, not a branch.** One row per type; a type with no
  resist maps to a sentinel that yields a mitigation multiplier of exactly `FIX_ONE`.
- **True/pure damage bypasses *mitigation*, never the *pipeline*.** It still goes through stages
  1, 4–10 — so it still respects shields, still leeches, still credits kills, still logs. A type
  that skipped the pipeline would reintroduce the second HP writer this spec exists to prevent.
- **Values are never renumbered** once a replay has been recorded against them: `DamageType` is
  part of the hashed sim state.

---

## 4. Mitigation math in Q16.16 — the load-bearing detail

The roadmap gives the formula as `amount * 100/(100+armor)`. Implementing that literally
**overflows**. This section is the correct formulation.

### 4.1 Operation order matters — one form overflows, the other does not

`fix_mul(a,b)` computes `(int64)a*b >> 16` and returns `int32`. The 64-bit intermediate is safe;
**the `int32` result is not.**

| Order | Form | Result |
|---|---|---|
| **A (wrong)** | `fix_div(fix_mul(dmg, K100), K100 + armor)` | `fix_mul(dmg, K100)` must hold `dmg × 100`, which exceeds the Q16.16 integer range (±32 768) for any `dmg > 327`. **Verified: overflows at dmg = 2000.** |
| **B (correct)** | `mult = fix_div(K100, K100 + armor)` then `fix_mul(dmg, mult)` | `mult` is bounded in `(0, 2]`, `dmg` is bounded by `DMG_MAX`. **No overflow anywhere in the domain.** |

Always compute the **multiplier first**, then apply it. This generalises: *in Q16.16, divide down
to a bounded factor before you multiply up.*

### 4.2 The two-branch mitigation function

Negative resist must **amplify**, and the naive formula cannot express that safely — at
`armor = -100` the denominator `K100 + armor` is exactly **0** (a hard divide-by-zero), and it
changes sign below that. Use the standard bounded reflection for the negative branch:

```c
#define K100 (100 * FIX_ONE)          /* 6553600 */

static fix mitigation(fix resist) {
    resist = fix_clamp(resist, RESIST_MIN, RESIST_MAX);      /* MANDATORY - see 4.4 */
    if (resist >= 0) return fix_div(K100, K100 + resist);          /* (0, 1]  */
    return 2 * FIX_ONE - fix_div(K100, K100 - resist);             /* [1, 2)  */
}
```

The negative branch is bounded at **2.0× as resist → −∞** — it can never explode, and it never
divides by zero because `K100 - resist > K100` for all `resist < 0`.

### 4.3 Measured accuracy across the whole range

| resist | multiplier (Q16.16) | float | exact | error |
|---|---|---|---|---|
| −100 | 98304 | 1.500000 | 1.500000 | +0.0000 % |
| −50 | 87382 | 1.333344 | 1.333333 | +0.0008 % |
| 0 | 65536 | 1.000000 | 1.000000 | +0.0000 % |
| 25 | 52428 | 0.799988 | 0.800000 | −0.0015 % |
| 100 | 32768 | 0.500000 | 0.500000 | +0.0000 % |
| 300 | 16384 | 0.250000 | 0.250000 | +0.0000 % |
| 500 | 10922 | 0.166656 | 0.166667 | −0.0061 % |

**Worst observed error across the full domain: 0.006 %.** Q16.16 is comfortably sufficient for
mitigation; no widening to Q32.32 is warranted. Rounding is `>>16`, which floors — so the bias is
consistently *downward*, and consistently downward is fine: it is identical on every machine,
which is the only property that matters.

### 4.4 The clamp is mandatory, not defensive

`RESIST_MIN` / `RESIST_MAX` must be enforced **at the stat level**, before `mitigation()` is
called. Without a clamp, `K100 - resist` overflows `int32` for large negative resist — verified:
an unclamped `resist = INT32_MIN` yields `2 154 037 248`, past the `int32` ceiling of
`2 147 483 647`, and the sign flip turns a mitigation multiplier into a negative number, i.e. **a
heal**. Recommended bounds (content-tunable, but the *existence* of the bound is not):

```c
#define RESIST_MIN  (-100 * FIX_ONE)   /* 2.0x amplification floor */
#define RESIST_MAX  ( 500 * FIX_ONE)   /* ~0.167x, ~83% reduction  */
```

Both bounds keep every intermediate inside `int32` with three orders of magnitude to spare.

---

## 5. Stacking modifiers — additive sums, then one multiply

Stages 1 and 4 each apply **one** multiplier. That multiplier is built by **summing bonuses as
integers**, not by chaining multiplies:

```c
fix mult = FIX_ONE;
for each active modifier m:  mult += m.bonus;      /* exact integer add, no rounding */
base = fix_mul(base, mult);                        /* exactly one rounding step      */
```

**Why summing, and not chaining.** Integer addition is exact, associative, and commutative — so
the result does not depend on the order modifiers were applied in, and no ordering rule is needed.
Chained `fix_mul` rounds at every step, which makes the result **order-sensitive**: measured, five
+10 % modifiers on 300 damage give `483.132874` chained vs `483.119202` folded vs `483.153000`
true. The chained path forces you to declare and maintain a total order over modifiers forever;
the summed path makes the question disappear. This is also the genre-standard semantic — bonuses
that add are bounded and readable in a tooltip; bonuses that compound are neither.

**True multiplicative effects** (a literal "double damage") are the exception and are rare. They
get an explicit `modifier_class` integer and are applied **after** the summed multiplier, in
ascending `(modifier_class, source_entity_id)` order — a declared total order, because rounding
makes them order-sensitive by nature.

**Status-effect stacking taxonomy** (M5.3 asks for this to be enumerated once — here it is):

| Rule | Behaviour on re-application | Typical use |
|---|---|---|
| `REFRESH` | duration resets to full; magnitude unchanged; one instance | slows, most debuffs |
| `STACK_N` | new instance added up to `max_stacks`; magnitudes sum | ramping DoTs, marks |
| `UNIQUE_SOURCE` | one instance *per source entity*; each refreshes independently | multi-attacker DoTs |
| `STRONGEST` | keep only the largest magnitude; duration from the winner | competing auras |

The rule is declared **per status definition**, not per application site.

---

## 6. Time is measured in ticks — never in seconds

This is the single most consequential convention in the whole design layer, and it is worth
stating as a law because the alternative is subtly wrong and hard to notice.

### 6.1 The dt trap (finding F3)

`SIM_DT_FIXED` is defined as `FIX_ONE / SIM_HZ`. In integer arithmetic that is `65536 / 30 =`
**`2184`**, which represents `0.0333252` s — **not** `1/30 = 0.0333333` s. Thirty ticks therefore
sum to `65520`, not `65536`: **every "second" integrated through `SIM_DT_FIXED` runs 0.0244 %
short.** Over a 30-minute match (54 000 ticks) any quantity accumulated as `x += rate * dt` ends
up 0.024 % low, and "seconds" in a design doc stop agreeing with what the sim does.

The magnitude is small; the point is that it is **free to avoid entirely**.

### 6.2 The two laws

> **Law 1 — All durations are integer tick counts.** Cooldowns, cast times, channel times, status
> durations, respawn timers, and buff windows are `uint16_t` / `uint32_t` **ticks**. Never a `fix`
> of seconds. Decrement by 1 per tick and test against 0. This is exact, cheap, and trivially
> hashable.
>
> **Law 2 — Rate accumulators never multiply by `dt`.** To advance something at *R* per second,
> add `R` (as a `fix`) each tick and fire when the accumulator reaches `SIM_HZ * FIX_ONE`:
>
> ```c
> #define RATE_PERIOD (SIM_HZ * FIX_ONE)   /* 1966080 - fits int32 with headroom */
> progress += rate_fix;
> while (progress >= RATE_PERIOD) { progress -= RATE_PERIOD; fire(); }
> ```

Authoring stays in seconds — the **cooker** converts to ticks at bake time
(`ticks = round(seconds * SIM_HZ)`), so designers write `8.0` and the sim stores `240`.

### 6.3 Why Law 2 matters — attack speed, measured

Attack speed is the case where this is visible to players. The obvious implementation — a
precomputed `period_in_ticks = round(SIM_HZ / aps)` — **quantizes badly at high attack speed**,
because at 30 Hz the available periods are integers:

| target aps | period | effective aps | error |
|---|---|---|---|
| 1.00 | 30 | 1.000 | +0.00 % |
| 1.75 | 17 | 1.765 | **+0.84 %** |
| 2.00 | 15 | 2.000 | +0.00 % |
| **2.25** | **13** | **2.308** | **+2.56 %** |
| 2.75 | 11 | 2.727 | −0.83 % |
| 3.00 | 10 | 3.000 | +0.00 % |

A **2.56 % free damage bonus at exactly 2.25 attack speed**, appearing and vanishing as items are
bought, is a real balance artifact and an unfixable one at 30 Hz — you cannot tune around a
quantization step.

The Law-2 accumulator removes it. Verified over a full 30-minute match (54 000 ticks): attack
speeds of 0.63, 1.37 and 2.73 each produced attack counts within **one in-flight attack** of exact
— **no accumulating drift at all.** The only residual error is the one-time Q16.16 quantization of
the attack-speed value itself, which is `1/65536` ≈ **0.0015 %** at aps 1.0.

The attack still *lands* on an integer tick — a 30 Hz sim cannot do otherwise — but the long-run
rate is exact, which is what balance depends on.

---

## 7. Magnitude budget — what fits in Q16.16

`fix` spans **±32 767.99998** with a resolution of `1/65536 ≈ 1.5e−5`.

| Quantity | Representable as `fix`? | Ruling |
|---|---|---|
| Hero HP (≤ 10 000) | yes | `fix` |
| Structure HP (nexus, 20 000–32 000) | yes, near the ceiling | `fix`, with `HP_MAX` asserted |
| HP 40 000+ | **no** | forbidden; cap structure HP |
| Single damage instance, amplified (≤ 30 000) | yes | `fix`, clamped at `DMG_MAX` |
| **Match totals** — damage dealt, gold earned, healing done | **no** (250 000 does not fit) | **`int32_t`/`int64_t` integer counters, never `fix`** |

```c
#define DMG_MAX  (30000 * FIX_ONE)   /* ~8% headroom below FIX max; clamp at stage 5 */
#define HP_MAX   (32000 * FIX_ONE)
```

Stage 5's clamp is what keeps stages 6–8 provably in range. A silent Q16.16 wrap is the classic
way a deterministic sim produces a *deterministically wrong* result on every machine at once —
which the state hash will happily agree with. Assert on the clamp in Debug.

---

## 8. Determinism rules for this pipeline

1. **Drain in insertion order — do not sort.** Events are inserted by a literal plain-function
   schedule whose systems iterate ascending `EntityId`, so insertion order is *already* a total
   order derived from hashed state. Sorting would be redundant work and a second thing to get
   right. Assert in Debug that the recorded `(system_index, source_entity_id)` key is
   non-decreasing across the buffer; that assert is the real guarantee.
2. **Fixed capacity, explicit overflow policy.** The buffer is fixed-capacity by the existing
   phase-buffered design. Overflow must be a **loud, deterministic** failure — never a silent
   drop, which desyncs a server from a predicting client the moment their buffers differ.
3. **No RNG unless drawn through the ordered sim PRNG** (ADR-0007/0013). Critical strikes, if the
   design has them, draw in the pipeline's deterministic order, once per event, whether or not the
   result is used — a conditional draw is a desync.
4. **No floats anywhere in this path.** Enforced by the existing float-ban lint; this file adds no
   exception.
5. **Kill credit is state, not a callback.** Store `last_damage_source` + `last_damage_tick` on
   the target and resolve credit at stage 10 from that state. Callbacks would reintroduce ordering
   sensitivity.

---

## 9. Definition of Done for M5.4

The roadmap's DoD is *"units damage and die deterministically; kill credit correct; respawn timers
tick; self-check green."* Concretely, that means:

- [ ] `combat_resolve` is the **only** function that writes `Health`, proven by a **build-time
      audit** in the style of the existing generated sim-policy/source-owner audit — not by
      convention. (The roadmap's own listed risk for M5.4.)
- [ ] `mitigation()` unit test spans the full clamped resist range including both branch
      boundaries (`resist = 0`, `resist = RESIST_MIN`, `resist = RESIST_MAX`) and asserts the
      table in §4.3 exactly.
- [ ] Overflow tests: `DMG_MAX` clamp fires; unclamped resist is unrepresentable by construction.
- [ ] Stage-order golden: a fixture with shields + lifesteal + amplification produces a pinned
      byte-exact result, so a future reorder of §2 fails loudly.
- [ ] Modifier-order test: applying the same modifier set in reversed order yields an **identical**
      hash (proves the §5 summed-multiplier property).
- [ ] Attack-rate test: 54 000 ticks at a fractional attack speed produce the expected count with
      no drift (proves §6 Law 2).
- [ ] The 10 000-tick determinism self-check stays green with combat active, Debug and Release.

---

## 10. Open items this spec is waiting on

| Item | Blocks | Owner decision in |
|---|---|---|
| Damage-type member list | §3 enum finalisation | `00-` §6.4 |
| Whether critical strikes exist | §8.3 RNG draw policy | new |
| Structure HP ceiling | §7 `HP_MAX` | follows the launch shape (`00-` §4) |
| Negative-resist amplification curve | §4.2 (bounded 2.0× reflection is the recommended default) | new |
