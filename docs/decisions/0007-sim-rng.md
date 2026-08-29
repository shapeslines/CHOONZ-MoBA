# ADR 0007 — Simulation RNG: pcg32 (sim) / xoshiro256** (presentation)

- **Status:** Accepted (2026-06-05)
- **Related:** [0002](0002-fixed-point-sim-math.md), [0011](0011-server-authoritative-netcode.md)

## Context

The simulation needs a random number generator that is **deterministic,
serializable, and integer-only** (composes with fixed-point, no float). Drafts
split between PCG32 and xoshiro256** for the sim.

## Decision

- **Sim RNG: `pcg32`** — defined once in `engine/math/include/math/rng.h`. State is
  two `u64`s, lives **inside `SimWorld`**, and is therefore hashed by `sim_hash` and
  serialized (so it round-trips through replays and reconciliation). Excellent
  statistical quality, tiny, integer-only.
- **Seeded from the server-owned match seed.** Under server-authority ([0011]) the
  authoritative server owns the match seed; it is embedded in replays. (No
  "all peers agree on a seed" — that was the lockstep model.)
- **Presentation/tools RNG: `xoshiro256**`** — a *separate* stream for VFX/UI/tools.
  Cosmetic randomness must never perturb gameplay, so it is physically distinct
  from the sim RNG and never touches `SimWorld`.

## Consequences

- Reproducible sim and replays; the sim/presentation RNG split is part of the
  determinism contract (ARCHITECTURE §2.1).
- Any sim code reaching for the presentation RNG (or vice-versa) is a bug the
  separation makes obvious.
