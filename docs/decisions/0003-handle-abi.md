# ADR 0003 — Handle ABI: 32-bit, 18-bit index + 14-bit generation

- **Status:** Accepted (2026-06-05)

## Context

Entities and GPU/asset resources are referenced by **generational handles**: an
index into a pool plus a generation counter bumped each time the slot is recycled.
A handle is valid iff its generation matches the slot's current generation — this
is how a stale reference to a destroyed-and-recycled object is *detected* instead
of silently dereferencing a different object.

We want one ABI shared across `EntityId`, `MeshHandle`, `TextureHandle`,
`AssetHandle`. The genre is a **MOBA/RTS hybrid** (see project context): higher
entity counts than a typical MOBA and, especially, **much higher slot churn** —
projectiles and effects spawn/die constantly, recycling pool slots heavily.

Two facts decide the split:

1. **Index headroom is over-provisioned at any reasonable split.** Peak
   *simultaneous* entities, even in extreme hybrid fights, sit in the low tens of
   thousands — far below what 16–20 index bits provide.
2. **The failure modes are asymmetric.** Index exhaustion is a *hard* failure (you
   cannot create the entity). Generation wrap is a *soft, detectable* failure (a
   slot's counter wraps after enough recycles; a debug assert catches it). So:
   provision index generously, generation amply.

## Decision

**32-bit packed handle: 18-bit index + 14-bit generation**, unified across all
handle types via one macro (identical index/gen extraction everywhere).

```
  [ index : 18 ][ generation : 14 ]      // 4 bytes
  262,144 simultaneous ids               // ~10-100x peak headroom for the hybrid
  16,384 generations per slot before wrap // sized for heavy projectile churn
```

- **Generation `0` is the null/invalid sentinel.**
- **Debug builds assert if any slot's generation is about to wrap** — it should
  never happen in a real match, so this catches pathological churn in development.
- Chosen over the earlier 20+12 by shifting two over-provisioned index bits into
  churn-critical generation bits, prompted by the higher-churn genre.

## Consequences

- **4 bytes keeps handles cache-dense** in the hot SoA loops this data-oriented
  engine is built around — which matters *more*, not less, at higher entity counts.
  This is why 64-bit (32+32, never-wraps but 8 bytes) was rejected.
- Smaller handles → smaller `sim_hash` and wire footprint (good for
  server-authoritative replication bandwidth).
- A single slot recycled >16,384 times in one match would wrap — extreme, and
  caught in dev by the wrap-assert.
- **Alternatives considered:** *24+8* — dominated (wastes index, only 256
  generations, risky for churn). *16+16* — viable (max churn safety) but trades
  index headroom, the harder-failure axis, so not chosen for a genre that may
  scale up. *64-bit 32+32* — bulletproof but 2x handle memory and worse cache
  density. **Escape hatch:** the single typedef allows widening to 64-bit if a real
  need appears (touches `sim_hash`/wire layout, so not free).
