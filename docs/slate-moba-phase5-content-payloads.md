# Phase 5 Slate - Typed Content Record Payloads (`content-typed-payloads`)

**Status:** in progress

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
- [ ] **S1** ADR-0016 + container (`MbaRecordView`, `mba_encode_record`, `mba_record_measure`,
  record validation in `mba_inspect`, `byte_io` migration, `eng::serialize` link).
- [ ] **S2** `content.h/.cpp` records, validation, codecs; `tests/assets/content_tests.cpp`.
- [ ] **S3** Cooker `--kind`, `key = value` parser, map copy; `cooker_cli_tests.ps1` extended.
- [ ] **S4** Goldens from `tests/assets/content_golden.py` (`assets/content/golden/*.mba`,
  `content_golden.inc`); C++ encode == Python bytes for hero, objective, economy.
- [ ] **S5** Runtime: `ASSET_TYPE_RECORD`, `asset_register_record`, `asset_get_record`, `asset_load`
  case; registry test; sandbox loads `hero_test.mba`; `content` target cooks it;
  `sandbox_baked_content_tests.ps1` extended.
- [ ] **S6** Gates: `/WX` ×3, `debug-asan` (14.44), clang-cl/UBSan, `assets_boundary` +
  `asset_parsers_boundary`, Vulkan smoke on the RTX 4070 Ti, oracle unchanged; ROADMAP; PR.

## Slice evidence

_(filled per slice)_
