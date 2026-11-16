---
type: repo-noticeboard
id: noticeboard-choonz-moba
status: active-draft
owner_seat: unassigned
effect_authority: none
repo: shapeslines/CHOONZ-MoBA
fleet_status: ALIVE
related:
  - GromEstate docs/FLEET-MAP.md row CHOONZ-MoBA
  - docs/next-session.md
---

# Noticeboard — CHOONZ-MoBA

> Derived routing surface for agents and owners. **Does not** mint OGIP rows,
> rewrite receipts, grant gates, or queue work — execution authority remains
> this repo's `AGENTS.md`, its lane owners, and the estate queues. GromAtlas
> owns estate existence and structure; GromEstate's FLEET-MAP is a projection.

## Quick info
| | |
| --- | --- |
| Role | From-scratch C++17/raw-Vulkan MOBA-RTS engine prototype: deterministic fixed-point sim, replay, offline SPIR-V renderer; off-spine by design |
| Canonical checkout | CHOONZ/CHOONZ-MoBA |
| FLEET-MAP status | ALIVE in the [GromEstate projection](https://github.com/shapeslines/GromEstate/blob/main/docs/FLEET-MAP.md) |
| Current front | `docs/next-session.md` and `docs/WORK-FRONTLOG.md`; check their freshness against current main and open PRs |
| Key surfaces | `AGENTS.md` · `ARCHITECTURE.md` · `docs/ROADMAP.md` · `docs/decisions/` (ADRs) · `docs/JOURNAL.md` |

## Route here when
- Work targets the MOBA/RTS engine itself: C++ modules under `engine/`, build presets, CI, tests, M-milestone lanes.
- Questions touch determinism, the replay codec, state hashing, or the sim/presentation seam.
- Renderer, offline SPIR-V, asset registry, or `.mba` cooker work needs doing or citing.

## Route NOT here when
- Estate census/structure — GromAtlas; the GromEstate `docs/FLEET-MAP.md` is a projection. OGIP owner gates — EstateBoard `_registry` (`python tools/ogip.py`).
- Data-spine, auth, database, or backend work — this repo is off-spine, isolated by design (`ARCHITECTURE.md`).
- Sibling repos CHOONZ and CHOONZ-mobile — separate repos with their own boards.

## Capabilities (what extends from here)
- None cross-repo: `ARCHITECTURE.md` records zero cross-repo dependencies; isolation is a feature, not a gap.
- In-repo contracts to cite (never import): the determinism contract, platform/renderer seams, replay codec + `moba_replay` CLI, CTest golden gates.

## Current state pointers
- [Next session](next-session.md) and [work frontlog](WORK-FRONTLOG.md) are handoff pointers; verify the pin and current branch before following a first action.
- [Open pull requests](https://github.com/shapeslines/CHOONZ-MoBA/pulls) carry the current candidate heads, checks, and integration order. PR #66 has landed.

## Stop signs
- Vulkan SDK in hosted CI (`find_package(Vulkan) → REQUIRED` flip) is owner-gated (`docs/sessions/2026-06-06-m2.0.md`, `docs/slate-moba-2026-08-11.md`); no OGIP rows are cited in this repo's own docs.
- FLEET-MAP row note (2026-09-05): the stray `CHOONZ/MOBA-proto` clone is ABSENT on F: — do not restore without owner.

## Quicklinks
- [README](../README.md) · [AGENTS.md](../AGENTS.md) · [ARCHITECTURE](../ARCHITECTURE.md) · [ROADMAP](ROADMAP.md)
- System-Architecture deep-dive (sibling repo): `../System-Architecture/projects/moba.md`

## Changelog
- 2026-09-07 — board seeded (estate repo-board rollout, EstateBoard tracker).
- 2026-11-16 — refresh the census authority and routing pointers against current main and PR state.
