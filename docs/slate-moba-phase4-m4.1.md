# Phase 4 M4.1 Slate — Offline Cooker and Unified `.mba` Runtime

- **Status:** active
- **Branch:** `codex/m4.1-cooker`
- **Base:** `ac4d2b416c36992a988f80981a2419eb8e0ad2fd` (`main`)
- **Pull request:** pending (PR #53 was consumed by the intervening security merge)
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
- The direct M4.0 registry loaders intentionally remain until S4 replaces them with
  the catalog-driven baked-only runtime in one vertical change.

### S3 — Deterministic cooker and generated catalog — next

### S4 — Catalog-driven baked-only runtime — pending

### S5 — CMake content gate and sandbox migration — pending

### S6 — Adversarial boundary hardening — pending

### S7 — Full acceptance and ready PR — pending

## Locked M4.1 decisions

- `.mba` begins at version 1 with literal bytes `MBA\0`.
- Texture and integer-PCM sound are the only M4.1 payload types.
- Runtime loading is catalog-driven and baked-only; TGA/WAV parsers remain available
  to the cooker and tests, and in-memory registration remains available for tests and
  procedural assets.
- Raw SPIR-V remains renderer-owned and loose under ADR-0008.
- `SIM_LOGIC_HASH` and replay bytes do not change.
