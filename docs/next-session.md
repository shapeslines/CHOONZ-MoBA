# MOBA-proto — next session

## State @ M4.1 final local acceptance · 2026-08-13 · DESKTOP-BK4F0OA/Codex

M4.0 is merged on `main` through PR #52. The isolated `codex/m4.1-cooker` branch and draft PR #54
contain M4.1 only; the historical root checkout and prior worktrees were preserved.

Implementation and local acceptance are complete:

- ADR-0015 locks deterministic little-endian `.mba` version 1 for one-mip RGBA8 textures and
  integer-PCM sounds. `eng_asset_parsers` is POD-only and links exactly `core + serialize`.
- `moba_cooker` accepts only the four required root/manifest arguments, preflights the entire sorted
  manifest and source set, and publishes rooted baked assets atomically before the generated catalog
  commit marker. STL stays private to the tool.
- `asset_load(AssetRegistry*, AssetId, AssetLifetime)` is the sole runtime file loader. Generated
  catalog validation and complete container ID/type/shape checks happen before durable allocation,
  refcount mutation, or renderer upload. Runtime TGA/WAV file loading is gone.
- `moba::content` builds configuration-local baked/catalog products before either sandbox compiles.
  Both variants load `ASSET_UV_TEST_TGA`; stale unlisted `.mba` files are unreachable.
- M4.2+ features remain excluded: PNG/DEFLATE, mip generation, glTF/meshes, packs, compression,
  incremental dependency graphs, hot reload, gameplay, networking, and simulation changes.

## Verified locally

- `/WX` Debug, RelWithDebInfo, Release: 48/48 CTests each. Debug-ASan: 48/48.
- clang-cl 19.1.5 + active UBSan: 6/6.
- Independent Debug/Release clean cooks are byte-identical:
  - `uv_test.tga.mba`: `43C058906AFD340F3A9E33A40ABC5D88D62516E3D4621A2C3B9D5E33B2ADC90A`
  - `asset_ids.gen.h`: `518FA7857829A0F23B84589ABBAFA024052FD7C63EEF963DB3E93D47CAAAD43F`
- Debug/Release oracle remains 10,000 ticks, 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`; controlled divergence remains exactly tick 4321,
  `position_x`, entity 7.
- Fresh-walk passes 48/48 and a 90-frame validation-clean screenshot run. Separate RelWithDebInfo
  RTX 4070 Ti proof loads baked texture ID `0x3d9bff0eddada061`, exits cleanly after 90 frames, and
  yields a visually inspected 1280×720 BMP, 2,764,854 bytes, SHA-256
  `25E85134E5226B7A8FD238C59AB6189B90A9CC14D8511059BD3FC7406F7B49C9`.

## First action

1. Treat the final closure commit as the only candidate head and obtain fresh-context security and
   acceptance verdicts against it.
2. Repair any finding without widening M4.1, rerun the affected and full gates, and update the exact
   candidate SHA.
3. Require every exact-head PR #54 CI/CodeQL check, then mark the PR ready. Merge remains a separate
   owner-approved action; verify tree identity and synchronize `main` after landing.
4. From green `main`, plan M4.2 as a separate cooker-only PNG/DEFLATE + mip-generation slate.

## M4.2 boundary

M4.2 should add strict PNG parsing and owned DEFLATE/inflate inside the cooker, generate mip chains
into the already-versioned texture descriptor shape, and leave the runtime `.mba` loader unchanged
where possible. Do not combine glTF/mesh upload, packs, compression, incremental cooking, hot reload,
async streaming, gameplay, or networking with that slate.
