# Launch feasibility — what actually stands between this engine and a launch

- **Status:** Draft for owner decision · 2026-08-25
- **Scope:** design/scoping layer only. Changes no engine code, no ADR, no roadmap.
- **Reads:** `docs/ROADMAP.md`, `docs/DECISIONS/*`, `docs/next-session.md`, vault `20 Projects/moba/`.

---

## 1. The finding in one paragraph

The engine is in good shape and moving fast. The problem is not the engine. It is that
**`docs/ROADMAP.md` ends before a launch begins**, and that **no game design content exists at
all** — no unit, no ability, no damage number, no map, no role list, no fiction. Phase 5 is
written as if that layer already exists ("validate against real arena design", "the stacking
taxonomy needs enumeration once real abilities exist", "a few hand-authored abilities"), and it
does not. The roadmap's terminal milestone, M7.4, is defined as *"a friend can play a full LAN
match"* — that is a prototype gate, not a launch gate. Everything a launch additionally requires
(content, art, audio, accounts, matchmaking, a server fleet, patching) appears on no roadmap,
in no ADR, and in no slice doc.

There is also a **genre-specific trap** that makes "just keep building" the wrong default: a 5v5
MOBA needs **ten concurrent humans** before it can be played once. Not sold once — *played*
once. A solo build cannot iterate on a design it cannot run.

---

## 2. Velocity — the encouraging half, with evidence

Delivered between `created: 2026-06-22` (vault frontmatter) and M4.0 local acceptance
`2026-08-13` — **52 days**: Phases 0, 1, 2 complete, plus M3.0–M3.4 and M4.0. That is 21
milestones.

Scoring the roadmap's own effort tags with relative weights (S=1, M=3, L=8, XL=16 — chosen to
match its stated scale: *S ≈ a sitting, M ≈ a week of evenings, L ≈ multiple weeks, XL ≈ a
month-plus*):

| Set | S | M | L | XL | Effort units |
|---|---|---|---|---|---|
| **Delivered** (M0.0 → M4.0) | 4 | 9 | 7 | 1 | **103** |
| **Remaining** (M4.1 → M7.4) | 2 | 12 | 7 | 3 | **142** |

103 units in 52 days ≈ **2.0 units/day observed**. The roadmap's own scale implies roughly
0.4 units/day for a solo dev on evenings — so observed throughput is running about **5× the
scale the roadmap was written against**. That is the single most important positive fact here.

Projecting remaining work at observed velocity:

| Model | Effective units | Calendar |
|---|---|---|
| Straight-line at observed velocity | 142 | **~10 weeks** to M7.4 |
| With a 2× risk multiplier on Phase 6 only | 203 | **~15 weeks** to M7.4 |

**Why Phase 6 gets a multiplier and the others do not.** Everything shipped so far is
single-process, single-machine, and fully observable — exactly the work where this velocity is
credible. Phase 6 is distributed-systems work: prediction/reconciliation parity, interest
management, lag compensation. Historical single-process velocity systematically under-predicts
it. Concretely, Phase 6 is **61 of the 142 remaining effort units (43%)** and carries **6 of the
8 remaining long-pole flags**.

> **Read this honestly:** ~10–15 weeks buys **M7.4 — a LAN-playable prototype**. It does not buy
> a launch. Sections 3 and 4 are about that gap.

---

## 3. The four things that are on no roadmap

Each of these is currently at zero and none appears in `ROADMAP.md`, the ADRs, or the vault
slice docs.

1. **Game design content.** No units, abilities, damage types, map, roles, or balance values
   exist. M5.3 and M5.4 cannot be built without them — they are the *inputs* to those
   milestones, not outputs. (Addressed in this commit: see `01-` and `02-`.)
2. **Art and audio production.** The renderer currently draws 500 cubes. The roadmap consumes
   assets (`M4.3 glTF`, `M7.0 bitmap font`, `M7.1 WAV mixer`) but never budgets their creation.
   For a MOBA this is normally the largest single line item in the whole project.
3. **Live-service infrastructure.** Accounts, matchmaking, a dedicated-server fleet, patching,
   telemetry, and anti-cheat operations. Phase 8+ lists "server?" as an *optional*. ADR-0011
   chose server authority, which makes a server fleet mandatory at launch, not optional — the
   ADR is right; the roadmap has not absorbed its consequence.
4. **The ten-player problem.** A 5v5 MOBA is untestable below ten concurrent humans, and
   unshippable below a population large enough to fill queues. The genre's launch failure mode
   is not weak sales — it is **empty queues, at which point the product does not function.**

---

## 4. Three launch shapes

These are mutually exclusive scoping choices, not a sequence. This is the owner decision.

> **DECIDED 2026-08-25 — Shape B + C.** A small-format arena, shipped against bots, with human
> PvP post-launch. The analysis below is retained as the reasoning behind that call; the concrete
> product definition and the resulting roadmap delta now live in
> [`04-launch-product.md`](04-launch-product.md), which supersedes this section as the active
> plan. Headline: **76 remaining effort units (~5.5 weeks) against 203 risk-adjusted** — a ~63 %
> reduction, with all six Phase-6 long-pole flags off the launch critical path.

### Shape A — full 5v5 MOBA

The implied current default. Three lanes, jungle, ~15+ champions, matchmaking, server fleet.

- **Buys:** the genre as players expect it.
- **Costs:** all four Section-3 gaps at full size; a multi-year horizon; and it launches directly
  into League/Dota, where a small-team launch has no realistic path to the concurrent population
  the format requires.
- **Verdict:** not feasible as a *first* launch. Reachable as a post-launch expansion of B.

### Shape B — small-format arena (**recommended**)

1v1 / 2v2 / 3v3 on one compact arena.

- **Cuts:** lanes → one or none · jungle → none · champions → 4–6 · matchmaking → lobby/invite
  codes · minion waves → optional.
- **Keeps:** everything that makes it a MOBA — abilities on cooldown, an economy, a map
  objective, a win condition, fog.
- **Why it is the lever:** it turns "needs 10 humans to test once" into "needs 2." A design you
  can actually play is a design you can actually iterate. It also reduces content cost roughly
  by the champion-count ratio, which is the dominant art line item.
- **Costs:** it is a smaller product, and it must be *deliberately* framed as a small-format
  arena rather than as a MOBA that came up short.

### Shape C — vs-AI first, PvP after launch

Ship the Shape-B arena against bots; add human PvP post-launch.

- **The lever:** it removes **Phase 6 entirely from the launch critical path** — 61 effort units
  (43% of remaining) and 6 of the 8 remaining long-pole flags, deferred behind a seam that
  ADR-0011 and M6.0 (`command source abstraction`) already anticipate.
- **Costs:** bot AI good enough to be fun is real design work (M5.5 covers minions/towers, not
  hero-level bots — the roadmap explicitly defers hero AI). It trades the hardest *engineering*
  risk for a genuine *design* risk.
- **Note:** B and C compose. B+C is the shortest credible path to something shippable.

---

## 5. Contradictions and constraints found while reading

Surfaced, not resolved — each is an owner call.

| # | Finding | Where | Why it matters |
|---|---|---|---|
| **F1** ✅ | **RESOLVED 2026-08-25 → the fixed effect vocabulary.** D-16 is deferred, not overturned; [`03-effect-vocabulary.md`](03-effect-vocabulary.md) §2 keeps the migration path open by construction. Original finding: **the ability authoring model was specified two incompatible ways.** `ROADMAP.md` M5.3 says *"abilities as data, not code — a fixed effect vocabulary, **no scripting VM**"*. The vault decision index locks **D-16** (interpreted Lua 5.4 no-JIT sandbox VM) and **D-10** (script over native primitives, lifecycle hooks). | `ROADMAP.md` M5.3, `moba_engine_decision_index.json` | These are different products with different costs. D-16 is `reversibility: medium` and still unbuilt (P4), so the choice is genuinely open — but it must be made *before* M5.3, because M5.3 is the milestone that implements it. |
| **F2** | **`SIM_MAX_UNITS = 64`** caps commanded units per tick; widening it is a deliberate replay-format + logic-hash break. | `ROADMAP.md` M5.2 note (G23) | Fine for heroes. Verify against the chosen Shape's unit counts *before* M5.2, not after — it is cheap now and a format break later. |
| **F3** | **`SIM_DT_FIXED = FIX_ONE/30 = 2184`**, which represents 0.0333252 s, not 0.0333333 s — **−0.0244 % per second**. | ADR-0001 + ADR-0002 | Any duration integrated as `x += rate * SIM_DT_FIXED` runs 0.024 % slow, so "seconds" and "ticks" disagree. Avoidable for free — see `01-` §6. |
| **F4** | **Two working checkouts of the same GitHub repo** (`shapeslines/CHOONZ-MoBA`) exist side by side: `CHOONZ/CHOONZ-MoBA` (dirty — modified `docs/next-session.md`) and `CHOONZ/MOBA-proto` (clean), both at `ac4d2b4`. | filesystem | Divergent handoff docs across two checkouts is exactly the "handoff that claims an artifact that is not there" failure the fleet `AGENTS.md` warns about. Hygiene, not urgent. |

---

## 6. What this commit adds, and what it still needs from you

**Added** — the design layer M5.3/M5.4 were blocked on, written against the locked spine and
inventing no content:

- `01-combat-damage-pipeline.md` — the single damage pipeline, fully specified in Q16.16, with
  the operation order, the overflow-safe formulation, and the tick-quantization laws. This is
  the M5.4 spec and the M5.3 dependency.
- `02-unit-ability-schema.md` — the concrete unit/ability table shape: exact fields, units,
  ranges, scaling, and the authoring contract.

**Added after the 2026-08-25 decisions:**

- `03-effect-vocabulary.md` — the M5.3 instruction set: 11 opcodes, selectors separated from
  effects, static bounds in place of a VM fuel meter, cook-time validation, DoD.
- `04-launch-product.md` — Shape B+C made concrete: the product table, the proposed role list,
  and the milestone-by-milestone roadmap delta.

**Decision log:**

1. ~~**Launch shape**~~ — ✅ **Shape B + C** (small-format arena, vs-AI first). See `04-`.
2. ~~**F1 — ability authoring model**~~ — ✅ **fixed effect vocabulary**. See `03-`.
3. ~~**The role list**~~ — ✅ **RATIFIED: `BRUISER` / `CARRY` / `BURST` / `SUPPORT`** (`04-` §2).
   Deliberately functional and carrying no lore commitment, which is what lets item 5 stay
   deferred without blocking anything.
4. ~~**The damage-type set**~~ — ✅ **RATIFIED: `physical` / `magic` / `true`** (`01-` §3).

**Also ratified 2026-08-25, closing the schema layer:** resource = **energy** (small pool, fast
regen) · `KIT_SLOT_COUNT` = **3** (two basics + ultimate) · `MAX_LEVEL` = **8**, where the rank
budget closes exactly at 8 points (`02-` §7.1). **No schema-blocking decision remains open** — what
is left is tuning and content, not shape.
5. ~~**Whether any fiction exists**~~ — ✅ **deferred by owner decision.** Item 3's role list is
   functional rather than fictional precisely so that this can stay deferred without blocking the
   schema, bot target priority, or the champion budget.
