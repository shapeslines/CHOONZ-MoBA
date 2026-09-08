# Phase 4 M4.1 Slate - Offline Cooker and Unified `.mba` Container

**Status:** complete on `main`

**Branches:** `codex/m4.1-cooker-restart` (cooker + codec), `codex/m4.1-sandbox-baked-texture`
(content target + sandbox consumption)

**PRs:** #57 — `M4.1: deterministic cooker and .mba container` (merged 2026-08-27 as `334c80e`,
accepted base `6571ee4`); #63 — `M4.1: bake sandbox texture through CMake content target`
(squash-merged 2026-09-02 as `4f66af1`)

**Base:** M4.0 landed as `f30dbb7` (PR #52); security consolidation `a2565ca`

## Goal

Land only M4.1 per `docs/ROADMAP.md` M4.1: a separate `cooker.exe` linking the POD-C `asset_parsers`
library, a versioned little-endian `.mba` v1 container (ADR-0015), one runtime `asset_load` entry
point switching on the type tag, byte-deterministic brute-force re-cook, and a CMake `content` target
that gates the game binaries. No PNG/inflate, glTF, hot reload, catalogs, gameplay, or networking.

## Baseline evidence

- Synchronized green `main` at `b4df6bd` before #63; clean working tree.
- Oracle unchanged throughout: 10,000 ticks, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`; no replay-byte change.

## Slice ledger

- [x] Move TGA parsing, `asset_id`, and the `.mba` codec into `engine/asset_parsers/` (POD-C only;
  `tests/assets/check_asset_parsers_boundary.cmake`).
- [x] `tools/cooker/` brute-force TGA → `.mba` baker with `/EHsc` STL-private exception policy
  (ADR-0009); `tests/assets/cooker_cli_tests.ps1`.
- [x] `.mba` v1 header (magic, version, type, id) and texture payload; bad magic/version/type/size
  rejected before allocation; `tests/assets/mba_tests.cpp`.
- [x] Unified `asset_load()` in `engine/assets/include/assets/assets.h` as the sole baked entry point.
- [x] ADR-0015 recorded in `docs/decisions/0015-mba-v1-container.md`.
- [x] CMake `content` target cooks `assets/uv_test.tga` into `content/<Config>/uv_test.mba` before
  either sandbox executable builds (PR #63).
- [x] Sandbox registry root points at the baked directory and loads only `uv_test.mba` through
  `asset_load` → registry → renderer upload callback (PR #63; `tools/sandbox/src/main.cpp`).
- [x] `sandbox_baked_content` test invokes the `content` target and compares the 16,432-byte output
  with two fresh cooker runs (`tests/assets/sandbox_baked_content_tests.ps1`).
- [x] Full local acceptance and exact-head hosted checks on `70515c4`; squash-merged as `4f66af1`.

## Locked decisions

- `.mba` v1 stays texture-only; typed hero/action/map/economy payloads are the
  `content-typed-payloads` slice (`docs/slate-moba-proto-design.md` §6) and require their own slate.
- Raw loose `.spv` remains renderer-owned under ADR-0008.
- No incremental cooking or dependency graph; every cook is brute-force and byte-identical.

## Slice evidence

### Cooker and container (PR #57)

- `engine/asset_parsers/include/assets/mba.h`, `src/mba.cpp`; `tools/cooker/src/main.cpp`.
- CTest entries `mba` and `cooker_cli` registered in `tests/CMakeLists.txt`.

### Content target and sandbox consumption (PR #63, head `70515c401d6a1395080876f80789fa8d14f8e004`)

- Canonical local CI (`tools/local-ci.ps1 -Configuration Debug`) on the clean head: configure
  1,248 ms, build 1,609 ms, CTest 22,599 ms; 48/48 passed.
- `uv_test.mba` SHA-256 `37E37469B97A14B29E4BB6B5A4F5CBD2DED7CE165523C2D1518FEF7B7A036066`, identical
  across the `content` target and two fresh cooker runs.
- Vulkan smoke on an NVIDIA GeForce RTX 4070 Ti with validation enabled:
  `sandbox.exe --frames 90 --screenshot out\m41-baked-sandbox.bmp` loaded the asset-managed 64×64
  material (`id=0x074b820f509cf09f`), rendered 64 objects in one scene draw, captured the screenshot,
  and exited cleanly after 90 frames.
- Hosted checks on the exact head: windows-msvc Debug / RelWithDebInfo / Release, windows-clang-cl
  (UBSan capability), fresh-walk, CodeQL, Analyze (actions, c-cpp) — all green.

### Close-out (2026-09-02)

- Owner authorized the squash-merge of #63 and the closure of superseded rescue PR #64.
- This slate, `docs/JOURNAL.md` Sessions 14–15, and the ROADMAP status flip landed via the PM
  baseline lane `lane/moba-pm-baseline/20260902`.
