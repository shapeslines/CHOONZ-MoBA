# ADR 0013 — Sim RNG draw policy (when systems may draw)

- **Status:** Accepted (2026-08-12)
- **Related:** [0007](0007-sim-rng.md), [0001](0001-tick-rate.md)

## Context

`sys_rng_advance` advances the sim `pcg32` once per tick, unconditionally (M3.2). The
hash stream is stable today because every build of every tick draws or advances the
same way. Once real gameplay lands (Phase 5: abilities, AI tie-breaks, on-hit rolls),
systems will want to *draw* from the RNG. Without a stated policy, two failure modes
appear:

1. **Conditional draws** — a system that draws only when some condition holds makes
   the stream depend on *which systems exist and fire*, so feature-flag differences
   between builds (or between client-predicted and server builds) fork the hash.
2. **Draw-order drift** — a system that draws from the RNG at a different point in the
   schedule than its peers agreed on changes every downstream draw, silently.

## Decision

- **The per-tick stream is a fixed script.** Any system that draws must draw the same
  number of values, in the same schedule position, every tick it runs. A draw inside a
  conditional is forbidden; hoist the conditional outside the draw (e.g. "roll the hit
  table every tick, use it only when the attack fires").
- **Draws are positional, not semantic.** A draw does not "belong" to a system; it is
  value *N* of the tick stream. Adding a draw in an earlier system shifts all later
  draws — a deliberate, hash-breaking change requiring the M3.4-style logic-hash bump.
- **`sys_rng_advance` stays unconditional.** It exists so the stream is non-degenerate
  even on ticks with no draws; it does not grant permission to draw conditionally.
- **New draws land at the end of the schedule** (before `sys_rng_advance`) so existing
  draws are undisturbed; extending the schedule's tail is the cheap, non-breaking
  evolution path.
- **Presentation/tools RNG is untouched by this ADR** (xoshiro256**, ADR-0007) — it
  may be drawn anywhere, any time; it never perturbs the sim stream.

## Consequences

- Feature-flagging a system that draws is a **hash break**, not a no-op — record it as
  a deliberate logic-hash bump (the M3.0 oracle discipline).
- The `sys_rng_advance` unconditional advance keeps empty ticks non-degenerate.
- Reviewers of a system that calls `pcg32_next` should ask: *is this draw conditional,
  or does it fire every tick in a fixed schedule position?*
