# Phase 4 M4.1 Slate — Offline Cooker and Unified `.mba` Runtime

- **Status:** active
- **Branch:** `codex/m4.1-cooker`
- **Base:** `ac4d2b416c36992a988f80981a2419eb8e0ad2fd` (`main`)
- **Pull request:** draft PR #54 (PR #53 was consumed by the intervening security merge)
- **Scope:** TGA + PCM WAV cooker, `.mba` v1, generated catalog, baked-only runtime
- **Excluded:** PNG, glTF, packs, compression, incremental cooking, hot reload, gameplay, networking, simulation changes

Every slice closes the same loop: name the observable done-condition, implement the
smallest vertical change, run focused tests and affected builds, repair failures
without widening scope, record evidence, commit/push the green checkpoint, then
advance.

## Slice ledger

### S1 — Land M4.0 and rebaseline — complete

Done-condition: the approved PR #52 tree is on green `main`; current `main` is green;
M4.1 owns one clean isolated worktree.

- PR #52 approved head: `308841a9922e24bd3043b27ccbf0689cbd9e7a64`, tree
  `40f9958155dd0e3a1478f0dde775d1c1cb129de3`.
- Squash landing: `f30dbb701623a9fd3d2d9aaf66b4de97ee5294c7`, same tree
  `40f9958155dd0e3a1478f0dde775d1c1cb129de3`.
- PR #52 post-merge CI and CodeQL passed on `f30dbb7`.
- Synchronized base includes subsequent security PR #53 at `ac4d2b4`; its push CI,
  CodeQL, Debug, RelWithDebInfo, Release, clang-cl/UBSan, and fresh-walk gates passed.
- Untouched local `/WX` baseline at `ac4d2b4`: Debug 43/43, RelWithDebInfo 43/43,
  Release 43/43. The registered determinism/oracle and exact-divergence gates passed
  in each configuration.
- M4.0 acceptance evidence retained from PR #52: Debug-ASan 43/43, clang-cl/UBSan
  6/6, oracle `0x637628abff59c823`, stream `0x6f381609f7e59f0c`, logic
  `0xab96814425ba80a4`, and an RTX 4070 Ti 90-frame validation-clean screenshot.
- Old branch/worktree preserved. Active writer:
  `.worktrees/m41-cooker` on `codex/m4.1-cooker`.

### S2 — Lock `.mba` v1 and shared codec — complete

Done-condition: ADR-0015, the POD-only `eng_asset_parsers` target, allocation-free
codec, known-byte goldens, round trips, and malformed-container matrix pass in Debug
and Release. Open a draft PR after the green checkpoint.

- ADR-0015 fixes literal `MBA\0`, version 1, the 32-byte outer header, the one-mip
  RGBA8 texture payload, and integer-PCM sound payload with explicit LE fields.
- Stable identity, TGA/WAV parsers, catalog PODs, and the allocation-free codec now
  live in `eng_asset_parsers`; the boundary CTest requires `core + serialize` and
  rejects platform/render/game/sim/STL/heap contamination.
- Known-byte texture/sound goldens, encode/inspect round trips, writer/output atomic
  failure, truncation/trailing input, and outer/typed-field corruption matrices pass.
- `/WX` Debug and Release builds pass; each configuration passes 45/45 CTest entries.
- Green checkpoint `d5cf3cf` was pushed and draft PR #54 opened.
- The direct M4.0 registry loaders intentionally remain until S4 replaces them with
  the catalog-driven baked-only runtime in one vertical change.

### S3 — Deterministic cooker and generated catalog — complete

Done-condition: a strict invocation preflights the complete manifest, publishes
asset containers before the generated catalog commit marker, produces identical
Debug/Release bytes, and leaves unchanged outputs untouched.

- `moba_cooker` accepts only the four required option/value pairs, normalizes and
  validates a CMake-generated sorted manifest, and supports only `.tga` and `.wav`.
- Preflight rejects non-canonical/unsorted paths, duplicate paths, IDs, symbols, or
  outputs, unsupported types, malformed source data, oversized manifests/sources,
  and aggregate/output arithmetic overflow before publication.
- Each source becomes `<logical-path>.mba`; the catalog is sorted by `AssetId` and
  publishes last as `assets/asset_ids.gen.h`. Existing byte-identical files are not
  rewritten.
- Rooted reads and writes bind every existing path component, reject reparses and
  escapes, and perform bounded same-handle reads plus create-only temporary,
  handle-based atomic replacement.
- The adversarial CLI fixture owns its temporary root through atomic
  `NtCreateFile(FILE_CREATE)` acquisition and handle-bound cleanup. Fresh security
  rereview returned PASS after the manifest-read and fixture-custody repairs.
- `/WX` Debug and Release builds pass; both configurations pass 46/46 CTest entries.
  Independent cooks, unchanged recook mtimes, and Debug-versus-Release byte
  comparison pass for one TGA and one PCM WAV fixture.

### S4 — Catalog-driven baked-only runtime — complete

Done-condition: initialization validates one immutable catalog view before mutation;
the sole file-loading API accepts an `AssetId`, verifies its catalog/container
identity, and preserves all durable state on failure.

- `AssetRegistryConfig` now carries a catalog view. Initialization requires strict
  ascending IDs, canonical unique logical paths, exact `<logical>.mba` names,
  supported types, ID/path agreement, and catalog storage disjoint from registry and
  arena backing before the first persistent push.
- `asset_load(registry, id, lifetime)` performs deterministic binary lookup, bounded
  rooted same-handle read, complete `.mba` inspection, and catalog ID/type matching
  before copying CPU payloads or invoking the texture upload callback.
- Registry-level TGA/WAV file loaders and source-parser includes are removed. Direct
  parsers remain available only through `eng_asset_parsers` for cooker/tests; the
  in-memory registration APIs remain for procedural assets.
- Baked TGA pixels and WAV PCM/metadata match direct parser output exactly. Duplicate
  global loads retain refcount behavior; missing/unlisted IDs, corrupt/truncated
  containers, valid ID/type mismatches, under-budget lifetime arenas, and renderer
  failure leave registry counts, arenas, refs, and retained renderer resources
  unchanged.
- The sandbox uses a short procedural registry bridge for this checkpoint so the
  loose loaders are already absent; S5 replaces that bridge with the generated
  catalog/content target.
- `/WX` Debug and Release builds pass and both configurations pass 46/46 CTest
  entries, including the strengthened runtime boundary scan.

### S5 — CMake content gate and sandbox migration — complete

Done-condition: a clean sandbox build first cooks configuration-local content, and
both sandbox binaries load the generated catalog entry with no source/procedural
fallback.

- `moba_content` exposes only the active configuration's generated include root and
  baked asset root. Its `content` dependency builds `moba_cooker`, generates the
  sorted manifest, publishes every `.mba`, and publishes `asset_ids.gen.h` last.
- Debug and Release independently emit `uv_test.tga.mba` SHA-256
  `43C058906AFD340F3A9E33A40ABC5D88D62516E3D4621A2C3B9D5E33B2ADC90A` and
  generated header SHA-256
  `518FA7857829A0F23B84589ABBAFA024052FD7C63EEF963DB3E93D47CAAAD43F`.
- `moba_sandbox` and `moba_sandbox_null` both link the content gate, initialize from
  `MOBA_ASSET_CATALOG`, and call `asset_load(ASSET_UV_TEST_TGA)`. The temporary
  procedural bridge and source-directory definition are gone.
- A clean Debug sandbox build visibly runs the cooker first; a repeated `content`
  build is current/no-op. The null sandbox runs two frames and reports the baked
  64x64 texture at ID `0x3d9bff0eddada061` before a clean shutdown.
- `/WX` Debug and Release builds pass; each configuration passes 47/47 CTest entries,
  including generated-output and asset-boundary checks.

### S6 — Adversarial boundary hardening — next

### S7 — Full acceptance and ready PR — pending

## Locked M4.1 decisions

- `.mba` begins at version 1 with literal bytes `MBA\0`.
- Texture and integer-PCM sound are the only M4.1 payload types.
- Runtime loading is catalog-driven and baked-only; TGA/WAV parsers remain available
  to the cooker and tests, and in-memory registration remains available for tests and
  procedural assets.
- Raw SPIR-V remains renderer-owned and loose under ADR-0008.
- `SIM_LOGIC_HASH` and replay bytes do not change.
