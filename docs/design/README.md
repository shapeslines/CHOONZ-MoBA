# `docs/design/` — the game design layer

`docs/ROADMAP.md` and `docs/DECISIONS/` cover **how the engine is built**. This directory covers
**what the game is** — the layer Phase 5 consumes and which did not previously exist.

| Doc | What it is | Milestone |
|---|---|---|
| [`00-launch-feasibility.md`](00-launch-feasibility.md) | What stands between this engine and a launch: velocity evidence, the four gaps on no roadmap, three launch shapes, and the contradictions found while reading. **Carries the decision log.** | scoping |
| [`01-combat-damage-pipeline.md`](01-combat-damage-pipeline.md) | The single damage pipeline — stage order, Q16.16 mitigation math, the tick-quantization laws, magnitude budget, determinism rules, DoD. | **M5.4** (and M5.3's dependency) |
| [`02-unit-ability-schema.md`](02-unit-ability-schema.md) | The unit/ability table shape: fields, units, ranges, level-table baking, states, kit, the cook-time contract. | **M5.3** |
| [`03-effect-vocabulary.md`](03-effect-vocabulary.md) | The closed instruction set abilities are authored from — 11 opcodes, selectors separated from effects, static bounds instead of a VM fuel meter. | **M5.3** |
| [`04-launch-product.md`](04-launch-product.md) | **The active plan.** Shape B+C made concrete: product table, ratified role list, and the milestone-by-milestone roadmap delta. | scoping |
| [`05-bot-ai.md`](05-bot-ai.md) | Hero bot AI — bots as a command source, perceiving through the vision query, scored actions, honest difficulty knobs, and the self-play balance harness. | **new** (`04-` §4) |

**Read order:** `04-` for what is being built and by when · `01-`/`02-`/`03-` for how ·
`00-` for why, and for the decision log.

## Conventions these docs hold to

- **Design before implementing.** A system gets a written spec before it is built; the doc is the
  source of truth and code follows.
- **Data-driven, always.** Balance values live in cooked data, never in C++. Tuning is a re-cook,
  not a recompile.
- **One damage formula, typed interactions.** Every ability resolves through the same pipeline;
  there are no per-ability damage special cases.
- **Schema first.** Adding the twentieth unit fills in a table; it does not invent structure.
- **No invented content.** These files define shapes, units and conversions. Roles, the roster,
  balance values and fiction are owner-authored — each is listed as an open item rather than
  guessed at.

## Status

**Decided 2026-08-25:** launch shape **B + C** (small-format 3v3 arena, vs-AI first) · ability
authoring via the **fixed effect vocabulary** (F1 → `03-`) · roles **`BRUISER` / `CARRY` /
`BURST` / `SUPPORT`** · fiction **deferred**.

**Still open:** the damage-type member list (`01-` §3) · resource kind, `MAX_LEVEL`,
`KIT_SLOT_COUNT` (`02-` §10) · distribution and the art/audio budget (`04-` §6).

**Ready to build now:** `M5.3` and `M5.4` are spec-complete and depend on nothing in Phase 4 —
they can start in parallel with the cooker rather than queueing behind it.

These are design documents. No engine code, ADR, or roadmap entry was changed to add them —
`ROADMAP.md` still describes the pre-decision plan, and `04-` §4 is the delta against it.
