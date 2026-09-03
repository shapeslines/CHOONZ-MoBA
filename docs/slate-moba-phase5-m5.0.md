# Phase 5 M5.0 Slate - Map Grid and `.mapdesc` Codec (`m5-map-navigation`)

**Status:** in progress (S0 baseline recorded; S1–S4 authored; S5 hash bump pending)

**Branch:** `lane/moba-m5.0-map/20260902` (worktree `GITHUB-ROOT/_worktrees/CHOONZ-MoBA-m5.0-map`)

**Plan of record:** `docs/plans/m5.0-map-navigation.md` (PR #65); ARC manifest
`docs/arc-m5.0-map-navigation-manifest.json`

**Base:** `main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe` (M4.1 accepted)

## Goal

Give the simulation its authoritative integer tile grid: `MapGrid` SoA storage in a caller-owned
arena, `MapCell` flags/cost/height, Q16.16 cell↔world conversions with floor semantics, the fixed
N,E,S,W,NE,SE,SW,NW neighbour order with the diagonal corner rule, `MapLane` waypoint storage with
fail-closed validation, spawn lookup, and a little-endian `.mapdesc` codec that rejects every
malformed stream at the first invalid boundary without mutating the grid. Then hash the map into
canonical state with one recorded `SIM_LOGIC_HASH` bump. No renderer, no navigation, no `.mba` change.

## Fence nuances recorded against the plan

- Capacities live in `engine/sim/include/sim/map.h` (`SIM_MAP_MAX_*`) next to the module, not in
  `sim_config.h`; `SimWorldConfig` gains `MapConfig map` (all-zero = empty map, the default).
- `tests/CMakeLists.txt` gets the `add_executable` source line as well as the `add_test`.
- `engine/sim/CMakeLists.txt` gets one `src/map.cpp` line; the link graph stays `core + math + serialize`.
- `AssetId` is unreachable from `eng_sim` (compiler-policy include allow-list); the map carries a
  plain `uint64_t map_id`.
- `tests/sim/map_golden.py` is an independent Python encoder that writes both
  `assets/maps/lane_test.mapdesc` and `tests/sim/map_golden.inc`; the C++ encoder must match it byte
  for byte (cross-implementation check of the §3.2 field order).

## Baseline evidence (S0)

- Untouched worktree at `4f66af1`, `cmake --preset ci` (Ninja Multi-Config, `/WX`).
- Debug: 48/48 CTest passed (20.3 s). Release: 48/48 CTest passed.
- Oracle pinned by `sim_determinism`: final `0x637628abff59c823`, stream `0x6f381609f7e59f0c`,
  `SIM_LOGIC_HASH 0xab96814425ba80a4`; controlled mutation `tick=4321 field=position_x entity=7`.

## Slice ledger

- [x] **S0** Baseline recorded above.
- [ ] **S1** `map.h/.cpp`: config validity, transactional `map_init`, `map_set_dimensions`,
  `map_set_cell`, cell↔world (centre; int64 floor-div), `SimWorld.map` + config + memory budget.
- [ ] **S2** `map_is_walkable`, `map_cost`, `map_flags`, `map_neighbors` (fixed order, corner rule).
- [ ] **S3** `map_add_lane` (≥2 waypoints, in-bounds, walkable + lane-flagged, unique lane id,
  capacity), `map_lane_waypoints`, `map_find_spawn` (ascending cell order).
- [ ] **S4** `.mapdesc` codec: `MAPD` magic, schema 1, header/cells/lanes in §3.2 order; decode is a
  validate-everything-then-apply walk; malformed matrix (truncated ×3, bad magic, bad version, bad
  dimensions, bad count, bad cell, bad lane, capacity, trailing) rejects with the grid untouched;
  golden byte-identical to `assets/maps/lane_test.mapdesc`; `content` target copies `assets/maps/`.
- [ ] **S5** Map hashed into canonical state after `hash_health`; `map_valid` in
  `canonical_world_valid`; `SimStateField` map entries + mirrored `diff_map`; `SIM_LOGIC_HASH` bumped
  once; every pin updated; Debug == Release on two identical loads + 10,000 ticks; mutation still
  `tick=4321 field=position_x entity=7`.
- [ ] **S6** Full matrix (`/WX` Debug/RelWithDebInfo/Release, `debug-asan`, clang-cl/UBSan),
  isolation lint, local green; ROADMAP status; JOURNAL Session 16; pin; PR.

## Locked decisions

- Zero-size map is valid and is the default; the placeholder world and every existing fixture keep
  their memory footprint. Gameplay slices size the map from the authored `.mapdesc`.
- Flags: walkable 0x01, blocked 0x02, lane 0x04, spawn 0x08, objective 0x10, block_vision 0x20,
  ramp 0x40; walkable and blocked are mutually exclusive; bit 7 is rejected.
- Cell size is any positive Q16.16; the default authored map uses 0.5 u. `height_cell` is an integer
  band; ramps are flagged, no slope math.
- The derived render heightfield (ROADMAP M5.0 second half) moves to a presentation slice.
- No Vulkan smoke is required for this milestone: no renderer file changes.

## Slice evidence

_(filled per slice as each lands)_
