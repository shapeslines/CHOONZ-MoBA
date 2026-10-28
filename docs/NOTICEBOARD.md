---
type: repo-noticeboard
id: noticeboard-choonz-moba
status: active-draft
owner_seat: unassigned
effect_authority: none
repo: shapeslines/CHOONZ-MoBA
fleet_status: ALIVE
related:
  - GromCodebase docs/fleet/FLEET-MAP.md row CHOONZ-MoBA
  - docs/next-session.md
---

# Noticeboard — CHOONZ-MoBA

> Derived routing surface for agents and owners. **Does not** mint OGIP rows,
> rewrite receipts, grant gates, or queue work — execution authority remains
> this repo's `AGENTS.md`, its lane owners, and the estate queues. Census
> authority: FLEET-MAP (GromCodebase `docs/fleet/`); this card routes, it does
> not rule.

## Quick info
| | |
| --- | --- |
| Role | From-scratch C++17/raw-Vulkan MOBA-RTS engine prototype: deterministic fixed-point sim, replay, offline SPIR-V renderer; off-spine by design |
| Canonical checkout | CHOONZ/CHOONZ-MoBA |
| FLEET-MAP status | ALIVE (row: canonical clone is CHOONZ/CHOONZ-MoBA; stray CHOONZ/MOBA-proto clone ABSENT on F: 2026-09-05 — do not restore without owner) |
| Current front | `docs/next-session.md` — Phase 3 + M4.0 assets merged on `main`; M4.1 cooker boundary defined (@ `ac4d2b4`, 2026-08-18) |
| Key surfaces | `AGENTS.md` · `ARCHITECTURE.md` · `docs/ROADMAP.md` · `docs/DECISIONS/` (ADRs) · `docs/JOURNAL.md` |

## Route here when
- Work targets the MOBA/RTS engine itself: C++ modules under `engine/`, build presets, CI, tests, M-milestone lanes.
- Questions touch determinism, the replay codec, state hashing, or the sim/presentation seam.
- Renderer, offline SPIR-V, asset registry, or `.mba` cooker work needs doing or citing.

## Route NOT here when
- Estate census/structure or OGIP owner gates — GromCodebase `docs/fleet/FLEET-MAP.md`; EstateBoard `_registry` (`python tools/ogip.py`).
- Data-spine, auth, database, or backend work — this repo is off-spine, isolated by design (`ARCHITECTURE.md`).
- Sibling repos CHOONZ and CHOONZ-mobile — separate repos with their own boards.

## Capabilities (what extends from here)
- None cross-repo: `ARCHITECTURE.md` records zero cross-repo dependencies; isolation is a feature, not a gap.
- In-repo contracts to cite (never import): the determinism contract, platform/renderer seams, replay codec + `moba_replay` CLI, CTest golden gates.

## Current state pointers
- `docs/next-session.md` — handoff @ `ac4d2b4` (2026-08-18): Phase 3 M3.0–M3.4 + security consolidation and M4.0 (PR #52) merged; M4.1 boundary defined (PR #54 closed unmerged).
- Open lanes (2026-09-07): M5 series PRs #68–#72 (map, command intake, content payloads, hero combat, lane objectives), CI runner #66, PM baseline #65.

## Stop signs
- Vulkan SDK in hosted CI (`find_package(Vulkan) → REQUIRED` flip) is owner-gated (`docs/sessions/2026-06-06-m2.0.md`, `docs/slate-moba-2026-08-11.md`); no OGIP rows are cited in this repo's own docs.
- FLEET-MAP row note (2026-09-05): the stray `CHOONZ/MOBA-proto` clone is ABSENT on F: — do not restore without owner.

## Quicklinks
- [README](../README.md) · [AGENTS.md](../AGENTS.md) · [ARCHITECTURE](../ARCHITECTURE.md) · [ROADMAP](../ROADMAP.md)
- System-Architecture deep-dive (sibling repo): `../System-Architecture/projects/moba.md`

## Changelog
- 2026-09-07 — board seeded (estate repo-board rollout, EstateBoard tracker).
