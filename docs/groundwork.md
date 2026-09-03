# CHOONZ-MoBA — groundwork

Longer-lived build status. The resume pin is `docs/next-session.md`. Tickets live in `docs/WORK-FRONTLOG.md`. Sequence lives in `docs/ROADMAP.md`. Fleet work-state index is GromCodebase `docs/fleet/GAP-REGISTER.md` (point; do not fork).

## Build status

- **Now:** Phases 0–3 and Phase 4 M4.0–M4.1 are complete on `main` (`4f66af1`, 2026-09-02). Real and
  tested: a raw-Vulkan 1.3 renderer (instanced 500-cube field, depth, debug overlay, null backend,
  screenshot readback, validation-clean on an RTX 4070 Ti); a platform-free 30 Hz Q16.16 simulation
  with arena-backed generational entities, sparse-set SoA pools, phase-buffered typed events, a literal
  schedule, canonical FNV-1a state hashing, replay v1 + `moba_replay` CLI, and a bit-identical
  10,000-tick oracle (`0x637628abff59c823`); a platform-owned fixed-step accumulator with `eng_game`
  snapshots and interpolation; `eng_assets` with portable `AssetId`, an arena-backed registry, direct
  TGA/WAV loaders; the `.mba` v1 container, the offline cooker, the unified `asset_load`, and a CMake
  `content` target that bakes `uv_test.tga` and feeds the sandbox. Structural gates: sim compiler
  policy, test/game binary parity, isolation lint, clang-cl/UBSan, ASan, fresh-walk, CodeQL. The
  playable-slice design contract (`docs/slate-moba-proto-design.md`) is ratified: schemas, the 11-step
  tick, a seven-slice DAG, and nine acceptance tests.
- **Not yet:** nothing gameplay-shaped exists. No `MapGrid`, no navigation, no per-command reject
  reasons, no typed `.mba` payloads (texture-only), no PNG/inflate, no glTF meshes, no hot reload, no
  heroes/abilities/creeps/towers, no netcode (`engine/net/` does not exist), no HUD. Docs-side: the
  vault `moba` and `game-design` notes overlap and need Carson's disposition; GAP-011 (the vanished
  in-tree `MOBA-proto` clone) awaits a closing receipt in GromCodebase.

## Up next

- **M5.0 `m5-map-navigation`** — integer `MapGrid`/`MapCell`/`LaneDef`, cell↔world conversions,
  deterministic neighbor ordering, `.mapdesc` codec, golden tests. Plan:
  [plans/m5.0-map-navigation.md](plans/m5.0-map-navigation.md); manifest
  `arc-m5.0-map-navigation-manifest.json`. Fence: map-owned files in `engine/sim/`, `tests/sim/`.
- **M5.1 `m5-command-replay`** — `Command`/`CommandReject`/`SimEvent`, ordering key, duplicate and
  stale rejection, live/replay parity. Plan: [plans/m5.1-command-replay.md](plans/m5.1-command-replay.md).
  Parallel to M5.0 only with disjoint file claims.
- **`content-typed-payloads`** — typed `.mba` payloads; gate (M4.1 acceptance) is now met; needs its
  own plan before claim. Then `m5-hero-combat`, `m5-lane-objectives` per the DAG in
  [plans/README.md](plans/README.md).

## Conventions settled 2026-09-02

- `docs/sessions/` is retired; per-milestone evidence lives in `slate-moba-*.md`, narrative in
  `JOURNAL.md`, extracted pins in `session-archive/`.
- Every milestone gets a `docs/plans/<id>.md` (fence, gate, slice ledger) before code, and a slate
  after. Branch `lane/moba-<id>/<yyyymmdd>` in a `GITHUB-ROOT/_worktrees/` worktree.

## Pointers

- Resume pin: [docs/next-session.md](next-session.md)
- Sequence: [docs/ROADMAP.md](ROADMAP.md) · Theory→repo bridge: [docs/plans/README.md](plans/README.md)
- Tickets: [docs/WORK-FRONTLOG.md](WORK-FRONTLOG.md) · Docs map: [docs/README.md](README.md)
- Architecture: [docs/ARCHITECTURE.md](ARCHITECTURE.md) · ADRs: [docs/decisions/README.md](decisions/README.md)
- Design authority (vault): `$OIP_VAULT/20 Projects/moba/moba.md`, `moba_engine_decision_index.json`, `slice-m*.md`
- Discovery: [FLEET-INDEX.md](https://github.com/shapeslines/GromCodebase/blob/main/docs/fleet/FLEET-INDEX.md)
