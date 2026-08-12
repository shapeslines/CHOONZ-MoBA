# MOBA-proto — place in the Shapes//Lines system

> Reference map back to the canonical System-Architecture library. This repo stays the
> authoritative home of its own implementation docs; the deep-dive linked below is the
> system-level synthesis. Generated 2026-06-17 — refresh from the library when it changes.

**Layer:** Game · standalone C++/Vulkan engine — **off-spine**, a separate universe from the Shapes//Lines data spine
**Role:** Custom from-scratch MOBA/RTS-hybrid game engine (C++17, raw Vulkan 1.3, own ECS, netcode, and math); no shared auth, no database, no GromDB relation — isolation is by design.
**Status:** Phase 3 M3.0–M3.1 complete through PR #15. The platform-free 30 Hz Q16.16 simulation now owns an arena-backed generational entity manager, typed sparse-set SoA pools, deferred tick-boundary destruction, canonical ECS state hash/diff, replay-v1 codec and CLI, and an exact 10,000-tick determinism proof. M3.2 systems/scheduling is queued separately in `docs/slate-moba-phase3-m3.2.md`.

**Canonical deep-dive:** https://github.com/shapeslines/System-Architecture/blob/main/projects/moba.md
&nbsp;&nbsp;(local sibling: `../System-Architecture/projects/moba.md`)
**Library:** [whitepaper](https://github.com/shapeslines/System-Architecture/blob/main/whitepaper.md) · [system-map](https://github.com/shapeslines/System-Architecture/blob/main/system-map.md) · [patterns](https://github.com/shapeslines/System-Architecture/tree/main/patterns) · [decisions (ADRs 0001–0009)](https://github.com/shapeslines/System-Architecture/tree/main/decisions)

## House patterns instanced
- **determinism-contract** — the one house pattern MOBA-proto instances, and it instances it globally: Q16.16 fixed-point everywhere in `eng_sim`, a 30 Hz fixed tick (ADR-0001), arena-backed generational identity plus sparse-set SoA state, `pcg32` inside `SimWorld` (hashed + replayed), a per-tick canonical FNV-1a hash with run-twice self-check, a sim-boundary CTest, and direct `eng_sim → core + math + serialize` isolation. The **sim / present seam** is its concrete shape: `fixed→float` happens in exactly one place (the present glue), and nothing downstream can feed back into the sim.

*Repo-own patterns (engine-internal, not the 9 house patterns; see `docs/ARCHITECTURE.md` + ADRs 0001–0012):* arena-first ownership · 32-bit generational handle indirection · the platform seam (`platform.h`) · the renderer seam (`renderer.h`) · parse-in-tools / load-binary assets · two-channel error handling (asserts vs result codes).

## Alignment with locked decisions
- **DATA-1 (Supabase only)** — N/A. No database; all game state is ephemeral in-memory `SimWorld`; persistent player data is explicitly deferred to Phase 8+.
- **AUTH-1 (Supabase Auth + RLS)** — N/A. No auth layer exists or is planned in the engine; matchmaking/anti-cheat is Phase 8+ and would be its own design.
- **TRUTH-1 (DB = truth · GromDB)** — N/A. A game engine, not a business-operations system; GromDB has no bearing here. The off-spine isolation is a feature, not a gap.

## Cross-repo dependencies
- None — off the data spine.

---
*This map records alignment status only — it does not resolve open forks (those ride as Phase-0 ledger entries in the relevant execution-roadmaps). NOTE: `docs/DECISIONS/README.md` (the in-repo ADR index) is stale — ADR-0012 (Vulkan 1.3 hard minimum, accepted 2026-06-11) exists as a file but is absent from the index. Do not edit that file here; it is flagged for housekeeping in the MOBA-proto repo itself.*
