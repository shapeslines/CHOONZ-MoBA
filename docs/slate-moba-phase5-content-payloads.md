# Phase 5 Slate - Typed Content Record Payloads (`content-typed-payloads`)

**Status:** complete on the lane; PR open, owner merge gated (Actions billing lock)

**Branch:** `lane/moba-content-payloads/20260903` (worktree `GITHUB-ROOT/_worktrees/CHOONZ-MoBA-content`)

**Plan of record:** `docs/plans/content-typed-payloads.md` (PR #65). **ADR:** `docs/decisions/0016-mba-typed-record-payloads.md`

**Base:** `main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe`. Independent of #68/#69: the `MAP` payload is
opaque `.mapdesc` bytes (the asset modules may not include `sim/`), so no sim file is touched.

## Goal

Widen `.mba` from texture-only to typed, validated content records — `HeroDef` (with nested
`ActionDef`/`EffectDef`), `ObjectiveDef`, `EconomyRule`, and opaque `MAP` — authored offline as
`key = value` text, cooked byte-identically by `cooker --kind`, validated fully at `mba_inspect`,
stored verbatim by the registry as `ASSET_TYPE_RECORD`, and loaded by the sandbox through `asset_load`.
No sim change; oracle untouched.

## Decisions recorded

- Container version stays 1; tags `HERO=5, OBJECTIVE=6, ECONOMY=7, MAP=8`; 16-byte record header.
- `mba_inspect` decodes and semantically validates records before publishing a view, so
  `asset_load` fails closed before any registry mutation.
- `eng_asset_parsers` links `eng::serialize`; `mba.cpp`'s hand-rolled LE helpers replaced wholesale.
- ADR-0010 generated `asset_ids.gen.h` + collision check stays a named follow-on slice.
- One `.mba` = one top-level record; `ActionDef`/`EffectDef` nest inside `HeroDef` per §3.3.

## Slice ledger

- [x] **S0** Baseline: Debug 48/48; oracle `0x637628abff59c823 / 0x6f381609f7e59f0c / 0xab96814425ba80a4`.
- [x] **S1** ADR-0016 + container (`MbaRecordView`, `mba_encode_record`, `mba_record_measure`,
  record validation in `mba_inspect`, `byte_io` migration, `eng::serialize` link).
- [x] **S2** `content.h/.cpp` records, validation, codecs; `tests/assets/content_tests.cpp`.
- [x] **S3** Cooker `--kind`, `key = value` parser, map copy; `cooker_cli_tests.ps1` extended.
- [x] **S4** Goldens from `tests/assets/content_golden.py` (`assets/content/golden/*.mba`,
  `content_golden.inc`); C++ encode == Python bytes for hero, objective, economy.
- [x] **S5** Runtime: `ASSET_TYPE_RECORD`, `asset_register_record`, `asset_get_record`, `asset_load`
  case; registry test; sandbox loads `hero_test.mba`; `content` target cooks it;
  `sandbox_baked_content_tests.ps1` extended.
- [x] **S6** Gates: `/WX` ×3, `debug-asan` (14.44), clang-cl/UBSan, `assets_boundary` +
  `asset_parsers_boundary`, Vulkan smoke on the RTX 4070 Ti, oracle unchanged; ROADMAP; PR.

## Slice evidence

### S1–S5 (one commit; slices were built and tested incrementally in the worktree)

- `mba.h/.cpp`: tags 5–8, `MbaRecordView` in `MbaAssetView`, `mba_record_is_kind`,
  `mba_record_measure`, `mba_encode_record`; `mba_inspect` validates the record header
  (`record_kind == type`, `reserved == 0`, `record_bytes == payload_bytes - 16`) and then runs the
  content decoder, requiring it to consume exactly `record_bytes`; LE I/O migrated to `byte_io`.
- `content.h/.cpp`: §3.3 records, validation, codecs; encoded sizes hero base 28, action base 32,
  effect 16, objective 24, economy 8 (the plan's 20/28 guesses were corrected by the encoder).
- Registry: `ASSET_TYPE_RECORD`, `asset_register_record` (verbatim copy, `meta0/1/2`, dedup),
  `asset_get_record`, `asset_load` case; no new SoA array, `asset_registry_memory_required` unchanged.
- Cooker `--kind`; authored `assets/content/{hero_test,tower_test,gold_rule}.txt`.
- Goldens: `tests/assets/content_golden.py` (independent encoder) → `assets/content/golden/*.mba`
  (220 / 72 / 56 bytes) + `content_golden.inc`; C++ `mba_encode_record` output, the cooker output,
  and the CMake `content` target's `hero_test.mba` are all byte-identical to it.
- Sandbox loads `hero_test.mba` after the texture and prints
  `sandbox: content record loaded kind=5 schema=1 bytes=172 id=0x203cc30dded9460e`.
- Tests: `content` suite 6 tests / 114 checks; `assets` 12 tests incl. the record load/reject/dedup/
  unload case; `cooker_cli` (record recook + golden equality, invalid record exit 1, bad `--kind`
  exit 2); `sandbox_baked_content` (record artifact == fresh bake == Python golden).

### S6 gates

- `/WX` matrix: Debug **49/49**, RelWithDebInfo **49/49**, Release **49/49**; `sim_oracle_probe`
  unchanged in every config (`final=0x637628abff59c823 stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4`).
- `debug-asan` (`vcvars64 -vcvars_ver=14.44`): **49/49**.
- `tools/check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan`: `CLANG_CL_DETERMINISM=PASS:
  ubsan=on`, oracle reproduced.
- `assets_boundary`, `asset_parsers_boundary` green (no `sim/`, `render/`, `std::` in the asset modules;
  sandbox keeps `asset_load(` and `uv_test.mba`).
- Vulkan smoke on the NVIDIA GeForce RTX 4070 Ti with validation on: `sandbox.exe --frames 90
  --screenshot out\smoke-content.bmp` → 90 clean frames, 64 objects, one scene draw, record line
  present, `SANDBOX_SMOKE=PASS` from the strict classifier.
