# `docs/design/` — the game design layer

`docs/ROADMAP.md` and `docs/DECISIONS/` cover **how the engine is built**. This directory covers
**what the game is** — the layer Phase 5 consumes and which did not previously exist.

| Doc | What it is | Milestone |
|---|---|---|
| [`00-launch-feasibility.md`](00-launch-feasibility.md) | What stands between this engine and a launch: velocity evidence, the four gaps on no roadmap, three launch shapes, and the contradictions found while reading. **Carries the open owner decisions.** | scoping |
| [`01-combat-damage-pipeline.md`](01-combat-damage-pipeline.md) | The single damage pipeline — stage order, Q16.16 mitigation math, the tick-quantization laws, magnitude budget, determinism rules, DoD. | **M5.4** (and M5.3's dependency) |
| [`02-unit-ability-schema.md`](02-unit-ability-schema.md) | The unit/ability table shape: fields, units, ranges, level-table baking, states, kit, the cook-time contract. | **M5.3** |

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

All three are **drafts pending owner decisions**, listed in `00-` §6 and `02-` §10. Nothing here
has been ratified, and no engine code, ADR, or roadmap entry was changed to add them.
