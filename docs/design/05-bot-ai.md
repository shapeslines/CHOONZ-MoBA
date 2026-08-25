# Hero bot AI — the launch product's opponents

- **Status:** Draft spec · 2026-08-25 · new milestone, costed as `L` in `04-` §4
- **Why it exists:** `ROADMAP.md` M5.5 covers minion and tower state machines and **explicitly
  defers hero AI**. The Shape-C decision (`04-`) makes bots the launch opponent, so hero AI moves
  from deferred to load-bearing.
- **Depends on:** M5.2 (the command system), M5.6 (the vision query), `03-` (abilities)

---

## 1. The architectural decision that makes everything else easy

> **A bot is a command source. It emits `Command`s through exactly the same path a player does.**

M6.0 already names this seam — *"command source abstraction + replay parity."* Shape C promotes
it from a Phase-6 milestone to the foundation of the launch product. Four consequences, all free:

1. **Bots are replay-compatible with no extra work.** A bot match records and verifies through the
   existing `moba_replay` CLI, because the recorded artifact is a command stream and the bot
   produced commands.
2. **Bots are the test harness for the command system.** M5.2's DoD becomes continuously
   exercised rather than tested once.
3. **The PvP migration is a source swap.** When Phase 6 lands, a network command source replaces a
   bot command source at the same seam. Nothing in the sim changes.
4. **Bots cannot cheat by construction** — see §2, which is the most important one.

---

## 2. Bots read the vision query, not the world

> **A bot perceives through the same M5.6 team-vision query the local renderer reads. It never
> touches `SimWorld` directly.**

This is a design decision, not a technical constraint, and it is worth the discipline:

- **A bot that reads full world state is a maphacking bot.** Players feel this immediately and
  cannot articulate it — the bot dodges a skillshot it could not have seen, rotates to a gank it
  had no vision of. It reads as cheating because it *is* cheating.
- **It forces difficulty to be tuned honestly.** If a bot cannot be made harder by giving it more
  information, the only knobs left are reaction, aim and discipline (§5) — which is what "harder
  opponent" should mean.
- **M5.6 already delivers exactly this query.** Its DoD names the vision result as *"the single
  source consumed by both the local renderer slice and (later) the server's per-client filter."*
  Bots become a third consumer of the same query, which also pressure-tests it before Phase 6
  depends on it for real fog.

Enforce it the way the sim's other boundaries are enforced: the bot module links against the
vision query and the command builder, and **not** against `SimWorld` directly. The existing
source/include/link ownership audit from M3.4 is the right mechanism.

---

## 3. Scored actions, not behaviour trees

`ROADMAP.md` M5.5 already rules out behaviour trees and GOAP for minions. For heroes, a finite
state machine is too rigid and a behaviour tree is too much machinery. The middle is a **scored
action list**:

```
decide(bot):
  1. PERCEIVE   read the team vision query -> visible enemies, allies, objectives
  2. SITUATE    reduce to a small set of fixed-point situation scalars
  3. SCORE      score every candidate action against those scalars
  4. SELECT     highest score; tie-break ascending (action_id, target_entity_id)
  5. EMIT       translate the winning action into Command(s)
```

**Situation scalars** (all Q16.16, all derived from visible state only):

| Scalar | Meaning |
|---|---|
| `own_hp_frac`, `own_resource_frac` | self-preservation pressure |
| `local_strength_ratio` | Σ visible ally threat ÷ Σ visible enemy threat, in a radius |
| `nearest_enemy_dist_sq` | engagement range gate |
| `objective_pressure` | how contested the current objective is |
| `ability_readiness` | how much of the kit is off cooldown |

**Candidate actions** — a fixed, small set, mirroring `03-`'s posture that a closed vocabulary
beats an open one:

`RETREAT` · `ENGAGE` · `POKE` · `LAST_HIT` · `PUSH_OBJECTIVE` · `USE_ABILITY(slot)` ·
`REPOSITION` · `RECALL`

Each action's score is a **weighted sum of situation scalars, with the weights in cooked data**.
That is the same law as everywhere else in this design: tuning bot behaviour is a re-cook, not a
recompile. It also means a per-role weight table (`04-` §2: `BRUISER` / `CARRY` / `BURST` /
`SUPPORT`) gives four distinct playstyles from one scorer — a `CARRY` weights `RETREAT` on
`nearest_enemy_dist_sq` far more steeply than a `BRUISER` does.

---

## 4. Determinism rules

The bot runs inside `sim_tick` and is hashed state, so it obeys the same laws as everything else.

1. **Iterate bots in ascending `EntityId`.** Same rule as every other system.
2. **Score in fixed-point.** No floats — the existing lint covers this path once the module is in
   the sim's translation-unit set.
3. **Tie-break explicitly** on `(action_id, target_entity_id)` ascending. Float-free scoring
   produces exact ties far more often than float scoring does, so this is a real code path, not a
   defensive one.
4. **RNG draws are unconditional and ordered.** A bot's aim-error draw (§5) happens **once per
   decision tick per bot, whether or not the chosen action uses it.** A conditional draw makes the
   PRNG stream depend on the decision, which desyncs replay the moment a bot decides differently.
   This is the single easiest way to break determinism in this module.
5. **Stagger decisions deterministically.** Bots re-decide every `DECISION_PERIOD` ticks, phased by
   entity id:

   ```c
   if (((tick + entity_id) % DECISION_PERIOD) == 0) decide(bot);
   ```

   This spreads cost across ticks **and** is a pure function of hashed state. A wall-clock or
   round-robin scheduler would not be.

**Budget.** At most six hero bots, re-deciding every `DECISION_PERIOD` ticks with a staggered
phase, means well under one full decision per tick. Cost is not a concern here; the reason to
stagger is that it is free and it keeps the pattern correct if unit counts ever grow.

---

## 5. Difficulty — the honest knobs

All four are content values in the difficulty table. None of them grants information or stats.

| Knob | Mechanism | Feels like |
|---|---|---|
| **Reaction delay** | ticks between a situation changing and the bot acting on it | slower to respond to a gank |
| **Aim error** | a deterministic angular offset on skillshot targeting, drawn from the sim PRNG | misses skillshots |
| **Decision cadence** | `DECISION_PERIOD` — how often it re-evaluates | commits to bad decisions longer |
| **Ability discipline** | probability of selecting the optimal ability vs. the second-best | wastes cooldowns |

**Explicitly forbidden as difficulty knobs:** extra vision, stat bonuses, reading enemy cooldowns
or resources it cannot see, or ignoring fog. If a difficulty tier needs one of these to be
challenging, the scorer is the problem — fix the scorer.

---

## 6. The payoff: determinism turns into a balance instrument

This is the part worth building the module carefully for.

Because the sim is deterministic, headless, and bit-identical Debug-to-Release, **bot-vs-bot
self-play runs headless at whatever speed the CPU allows, with no renderer**. The existing
`--sim-self-check` path already proves headless sim parity. That gives an automated balance
harness essentially for free:

- run N thousand matches across every champion pairing
- record win rate, match length distribution, gold curves, ability usage
- flag any champion outside a tolerance band **before a human plays a single game**

For a solo build with 4–6 champions, this is the difference between balancing by intuition and
balancing by measurement. It does not tell you whether the game is *fun* — nothing automated
does — but it reliably catches "this champion wins 78 % of its matchups," which is the failure
mode that kills a small roster.

**Sequencing note:** the harness is a *consumer* of the bot module and the replay CLI, both of
which already exist or are spec'd. Build it immediately after the bots work at all, not at the
end — its value is highest during balance iteration, not after it.

---

## 7. Definition of Done

- [ ] A bot plays a full match end to end and the match **records and verifies** through
      `moba_replay` with no bot-specific handling.
- [ ] **Boundary audit:** the bot module has no link or include path to `SimWorld` — enforced by
      the M3.4-style generated ownership audit, not by convention (§2).
- [ ] **Determinism:** two runs of the same bot match hash-match, Debug and Release; the 10 000-tick
      self-check stays green with bots active.
- [ ] **RNG-order test:** a bot that changes its decision mid-match still consumes the same number
      of PRNG draws (proves §4.4).
- [ ] **Tie-break test:** a hand-built exact-tie fixture resolves identically across runs.
- [ ] **Vision honesty test:** a bot does not react to an enemy outside its team's vision — an
      enemy approaching through fog produces no behaviour change until it is visible.
- [ ] **Role differentiation:** the four role weight tables produce measurably different behaviour
      (engage distance, retreat threshold) on the same map and champion.
- [ ] Self-play harness runs N matches headless and emits per-champion win rates.

---

## 8. Open items

| Item | Blocks | Note |
|---|---|---|
| `DECISION_PERIOD` value | §4.5 | tune after the first bot works; start coarse |
| Difficulty tier count and names | §5 | product decision, `04-` §2 |
| Whether last-hitting exists | the `LAST_HIT` action | depends on whether minion waves ship (`04-` §4, M5.5) |
| Win-rate tolerance band | §6 | a balance-policy decision, not an engineering one |
