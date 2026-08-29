# ADR 0010 — Asset id scheme: hashed string path + generated constants

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §11

## Context

Runtime code must name a baked asset (mesh, texture, sound, ability table). The
candidates were a **hashed string path** vs **sequential manifest integers**.

Key realization: **asset lookups are a cold path.** Code resolves an id → a
resource *handle* ([0003]) once, at load/reference time, then uses the handle every
frame. So "direct array-index lookup speed" — the main argument for manifest ints —
is irrelevant here. The decision reduces to **ergonomics + safety**, and the best
answer takes the good half of each candidate.

## Decision

**64-bit hashed string-path ids, plus a cooker-generated constants header.**

- `asset_id("units/hero.glb")` → a stable 64-bit id (FNV-1a).
- The **cooker** (a) verifies there are **no hash collisions** across all assets at
  build time (a collision is a build error, not a runtime surprise), and (b) emits
  a generated header `asset_ids.gen.h` of `constexpr AssetId` constants **plus a
  debug id→name table** for readable logs/errors.
- Reference assets by the **generated constant** (`ASSET_UNITS_HERO_GLB`) for
  compile-time typo safety, or by **literal path** (constexpr-hashed) for quick
  iteration.
- Runtime resolution goes through our own `HashMap` (cold path — fine).

## Consequences

- Hashed-path **ergonomics** (no hand-maintained manifest — add a file and use it)
  + manifest-int **safety** (generated constants won't compile if mistyped) +
  **debuggability** (the id→name table answers "what is asset `0x9f3a...`?").
- A build step generates the constants header. Renaming an asset changes its id and
  requires updating references — the same as any id scheme.
