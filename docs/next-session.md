# MOBA-proto — next session

## State @ M4.1 closure candidate · 2026-08-25 · DESKTOP-BK4F0OA/Codex

M4.0 is merged on `main` through PR #52. The isolated `codex/m4.1-cooker` branch and draft PR #54
contain M4.1 only; the historical root checkout and prior worktrees were preserved. The exact
`codex/m4.1-cooker` delta from `51c228f` is reconciled as one M4.1 closure candidate; its full
commit SHA is the branch tip produced by the closure commit.

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

- `/WX` Debug, RelWithDebInfo, Release: 49/49 CTests each. Debug-ASan: 49/49.
- clang-cl 19.1.5 + active UBSan: 6/6.
- Windows PowerShell 5.1 targeted `cooker_cli` passes, including the .NET SHA-256 `abc`
  known-vector assertion; no executable hash-cmdlet dependency remains.
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
- The closure-head fresh-walk, exact-head hosted checks, and ready-state promotion remain the
  final gates for this candidate; merge is not authorized.

## First action

1. Use the single closure commit at this branch tip as the only candidate head; obtain fresh-context
   security and acceptance verdicts against that exact SHA.
2. Require every exact-head PR #54 CI/CodeQL check, then mark the PR ready. Merge remains a separate
   owner-approved action; do not synchronize `main` in this slice.
3. Leave M4.2 unopened until the owner lands this green head and starts a separate slate.

## M4.2 boundary

M4.2 should add strict PNG parsing and owned DEFLATE/inflate inside the cooker, generate mip chains
into the already-versioned texture descriptor shape, and leave the runtime `.mba` loader unchanged
where possible. Do not combine glTF/mesh upload, packs, compression, incremental cooking, hot reload,
async streaming, gameplay, or networking with that slate.
