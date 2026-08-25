# The launch product — small-format arena, vs-AI first

- **Status:** Owner decision recorded 2026-08-25; the specifics below are **proposals awaiting
  ratification** (§6). · **Supersedes:** `00-` §4 as the active shape.
- **Decision:** launch **Shape B + C** — a small-format arena, shipped against bots, with human
  PvP as a post-launch addition.

---

## 1. What was decided, and what it buys

| Decision | Effect |
|---|---|
| **Small-format arena** (not 5v5) | Testable with 2 people instead of 10. A design you can play is a design you can iterate. Cuts the content budget roughly by the champion-count ratio. |
| **Ship vs bots first** | Removes **all of Phase 6** from the launch critical path — 61 effort units and 6 of the 8 remaining long-pole flags — behind the M6.0 command-source seam that already anticipates it. |
| **Fixed effect vocabulary** (F1) | No sandbox, no fuel meter, no bytecode cooking, no extension of the determinism gate to script state. See `03-`. |

### The headline number

| Plan | Remaining effort units | At observed velocity (2.0/day) |
|---|---|---|
| Original roadmap → M7.4 (LAN prototype) | 142 | ~10 weeks |
| Original, risk-adjusted (2× on Phase 6) | 203 | ~15 weeks |
| **Shape B+C → a shippable vs-AI arena** | **76** | **~5.5 weeks** |

**A ~63 % reduction against the risk-adjusted plan**, and the removed portion is the part with the
highest variance. §4 shows the derivation line by line.

**The honest cost:** this trades the hardest *engineering* risk (distributed prediction and
reconciliation) for a genuine *design* risk (bot AI that is actually fun). `ROADMAP.md` M5.5
covers minion and tower state machines and **explicitly defers hero AI** — so hero-level bots are
new work, not a discount. They are costed in §4 as a full `L`.

---

## 2. The product (proposal — §6.1)

| Attribute | Proposal | Why |
|---|---|---|
| **Format** | 3v3 | Enough for roles to matter and for teamfights to exist; small enough that three humans, or one human and bots, can fill it. 1v1 is a duel, not a MOBA. |
| **Opponents at launch** | 3 bots; humans may fill any of the 3 allied slots locally | Delivers the Shape-C decision while keeping co-op playable from day one. |
| **Map** | one compact arena, single lane, no jungle | The map is the single largest hidden art and design cost. One arena, iterated, beats three that are each half-finished. |
| **Roster** | 4–6 champions | Below 4 there is no team composition; above 6 the art budget dominates before the game is proven. |
| **Match length** | 12–18 minutes | Long enough for a levelling and item arc; short enough that a bad match is cheap and iteration is fast. |
| **Win condition** | destroy the enemy core | The genre's readable default. Towers as the intermediate objective. |
| **Economy** | gold from kills and objectives; a small item set | Minion last-hitting is optional — it depends on whether minion waves ship (§4, M5.5). |
| **Levelling** | per-match levels with ability ranks | Feeds `02-` §4's baked level table, which already assumes it. |

### Roles — ✅ RATIFIED 2026-08-25

Roles are owner-authored by rule; the schema may not mint them. **Owner-ratified minimal
orthogonal set** for 3v3 with 4–6 champions:

| Role | Function | Distinguished by |
|---|---|---|
| `BRUISER` | melee frontline, sustained damage, closes distance | survivability + engage |
| `CARRY` | sustained ranged damage, fragile | damage over time, needs positioning |
| `BURST` | high damage on cooldowns, low sustained output | spike damage, cooldown-gated |
| `SUPPORT` | heal/shield/utility, low damage | enables others |

Four roles over 4–6 champions means every champion is distinct and a 3-stack has real composition
choices without any role being unrepresented. This set is deliberately *functional*, not fictional
— it carries no lore commitment, and it drives `02-` §2.1, bot target priority (`M5.5`), and the
champion budget above.

---

## 3. What "launch" means here

Shape B+C is shippable, but it is a **small competitive arena against AI**, and it must be framed
as exactly that. The failure mode to avoid is shipping it described as a MOBA and being read as a
MOBA that came up short.

**In the box at launch:** one arena · 4–6 champions · 3v3 vs bots · local co-op for the ally slots
· match rules, levelling, items · HUD, audio, packaging.

**Explicitly post-launch:** human PvP (Phase 6) · matchmaking and accounts · additional
champions · additional maps · ranked.

**Still an owner decision (§6.3):** the distribution channel and price. That choice constrains
packaging (M7.3) and should land before M7.3, not after.

---

## 4. Roadmap delta — the derivation

Effort units use the `00-` §2 weights (S=1, M=3, L=8, XL=16).

| Milestone | Original | Under B+C | Change |
|---|---|---|---|
| M4.1 cooker + `.mba` | L = 8 | L = 8 | — |
| M4.2 PNG in cooker | L = 8 | L = 8 | **optional** — TGA-only ships; PNG is an art-pipeline convenience |
| M4.3 glTF → mesh | L = 8 | L = 8 | — required for real art |
| M4.4 hot reload | M = 3 | M = 3 | **keep** — this is a balance-iteration multiplier, and Shape B+C is iteration-heavy |
| M5.0 map grid | M = 3 | M = 3 | — |
| **M5.1 movement** | **XL = 16** | **M = 3** | **reduced** — see below |
| M5.2 selection + orders | M = 3 | M = 3 | — |
| M5.3 abilities | L = 8 | L = 8 | — spec'd in `03-` |
| M5.4 combat | M = 3 | M = 3 | — spec'd in `01-` |
| M5.5 minion/tower AI | M = 3 | M = 3 | — towers required; minion waves optional |
| M5.6 vision/fog | M = 3 | M = 3 | **keep** — it is gameplay, not just a netcode input; its server-filter half defers with Phase 6 |
| **M6.0 – M6.7 netcode** | **61** | **0** | **deferred entirely** |
| **NEW — hero bot AI** | — | **L = 8** | **added** — M5.5 explicitly defers hero AI. Spec'd in [`05-bot-ai.md`](05-bot-ai.md). |
| M7.0 – M7.4 slice | 15 | 15 | — |
| **Total** | **142** | **76** | **−66 units (−46 %); −63 % vs the 203 risk-adjusted** |

### The M5.1 reduction is a scope cut, not a free win

M5.1 is XL because of **flow fields** — integer Dijkstra with a bucketed priority queue plus an
LRU field cache, sized for *"200+ units flowing to a shared goal."* That requirement comes from
5v5 lane minion waves. Under 3v3 with at most six heroes and small-or-absent waves, the milestone
reduces to its other two components: **grid A\* for heroes** (already in the milestone) plus the
**fixed-point separation pass** (needed regardless, so units do not overlap).

**Flow fields come back if minion counts exceed roughly 30 per side.** Record that as the trigger
so the decision is revisited by a number rather than by feel. Keep the movement seam shaped so
flow fields drop in behind it — the milestone already describes it as *tiered*, which is exactly
the right structure for deferring one tier.

### Sequencing note

Nothing in Phase 4 blocks `01-`/`03-`. **M5.3 and M5.4 can start now** — their specs are written,
and neither needs the cooker. That matters: it means design and content iteration can begin in
parallel with M4.1 rather than queueing behind it.

---

## 5. What this does not solve

Two of the four gaps in `00-` §3 are reduced by this decision. Two are not.

| Gap | Status under B+C |
|---|---|
| Game design content | **addressed** — `01-`, `02-`, `03-` are the systems layer; content authoring unblocks once §6 lands |
| The ten-player problem | **solved** — 3v3 vs bots needs one human |
| **Art and audio production** | **still open, still the largest line item.** 4–6 champions plus one arena is a far smaller budget than 15 plus three, but it is not small, and it appears on no roadmap. This needs its own estimate before a launch date means anything. |
| **Live-service infrastructure** | **deferred with Phase 6**, not solved. A vs-AI product needs no server fleet — which is most of why this shape is feasible. It returns in full when PvP does. |

**The remaining honest risk:** hero bot AI good enough to carry a launch is a design problem with
no determinism gate to tell you when it is done. Budget iteration time for it explicitly, and
treat "is this fun against bots?" as a gate before M7.3 packaging — not after.

---

## 6. Open items

| # | Item | Blocks | Note |
|---|---|---|---|
| **6.1** | Ratify the §2 product table — format, match length, win condition, economy | content authoring, M7.2 match rules | proposals with rationale; edit freely |
| ~~6.2~~ | ~~Ratify the §2 role list~~ | — | ✅ **RATIFIED 2026-08-25** — `BRUISER` / `CARRY` / `BURST` / `SUPPORT`. `02-` §2.1, bot target priority (M5.5) and the champion budget are unblocked. |
| **6.3** | Distribution channel and price | M7.3 packaging | should land before M7.3 |
| **6.4** | Art and audio budget and pipeline | any launch date | §5 — the largest unmeasured item |
| **6.5** | `02-` §10 items 2–4: resource kind, `MAX_LEVEL`, `KIT_SLOT_COUNT` | `02-` §3, §4, §7 | small, cheap, high-leverage |
| **6.6** | Fiction — deferred by owner decision | naming, art direction | roles (§6.2) intentionally carry no lore commitment, so this can stay deferred without blocking |
