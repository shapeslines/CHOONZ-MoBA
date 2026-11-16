# Plan — `content-typed-payloads`

**Status:** open, gate met (M4.1 accepted `4f66af1`; M5.0 defines the map bytes). **Branch:**
`lane/moba-content-payloads/<yyyymmdd>` in a `GITHUB-ROOT/_worktrees/` worktree from green `main`
after PR #68 merges. **Slate to open:** `docs/slate-moba-phase5-content-payloads.md`.

## Goal

Widen the `.mba` container from texture-only to typed, validated game-content payloads so heroes,
actions, effects, maps, and economy rules are authored offline, cooked byte-identically, loaded
through the single `asset_load` entry point, and referenced by stable `AssetId` — the content the
`m5-hero-combat` and `m5-lane-objectives` slices bake against. No gameplay behavior in this lane.

## Spec sources (restated, not re-derived)

- `docs/slate-moba-proto-design.md` §3.1 scalar rules; §3.3 `HeroDef`, `ActionDef`, `EffectDef`,
  `ObjectiveDef`, `EconomyRule` (field order = serialized order; bounded arrays with explicit counts;
  maps/sets sorted by stable id); §3.2 `MapGrid` (already a byte format: `.mapdesc`, PR #68); §6 row
  `content-typed-payloads`; §7.2 item 8 (valid content recooks byte-identically; malformed, duplicate,
  unknown-version, bad-reference data fails closed).
- `docs/decisions/0015-mba-v1-container.md` (32-byte header, texture-only payload, `asset_load` sole
  entry) — superseded for payload kinds by a new **ADR-0016 `.mba` v2 typed payloads** written as S1.
- `docs/decisions/0010-asset-id-scheme.md` (normalized-path FNV-1a/64 ids + generated constants),
  ADR-0009 (POD-C `asset_parsers`, `/EHsc` STL-private cooker).
- Vault seed `20 Projects/moba/slice-m4-offline-asset-cooker.md`; ROADMAP M4.1 "typed payloads".

## Write fence

- `engine/asset_parsers/include/assets/mba.h`, `src/mba.cpp` (type tag widening, per-kind payload
  codecs, versioned per kind), new `include/assets/content.h` / `src/content.cpp` for the §3.3 records.
- `engine/assets/include/assets/assets.h`, `src/assets.cpp` — `asset_load` type switch + per-kind
  validation before any registry mutation.
- `tools/cooker/src/main.cpp` — `--kind hero|action|effect|map|economy` inputs (a small line-based
  authored text format, one record per file) → `.mba`.
- `tests/assets/` (unit + golden + malformed matrix), `tests/CMakeLists.txt` (`add_test` + source line),
  `assets/content/` goldens, `tools/sandbox/CMakeLists.txt` `content` target extension.
- `docs/decisions/0016-mba-v2-typed-payloads.md`, `docs/decisions/README.md` index row, the slate.

## Out of scope

Any `engine/sim/` change (consumers come in `m5-hero-combat`); PNG/inflate (M4.2); glTF (M4.3);
catalogs/incremental cooking; a GUI editor; balance values themselves (held per proto-design §2.3).

## Slice ledger

- [ ] **S0 Baseline.** Matrix + oracle on the merged `main` (post-#68 values).
- [ ] **S1 ADR-0016 + container.** `MbaType` gains `HERO, ACTION, EFFECT, MAP, ECONOMY`; header
  version stays 1 but each payload carries its own `schema_version`; unknown type/version rejects
  before allocation. ADR-0016 records the rule "one `.mba` = one record; sets are directories".
- [ ] **S2 Records + codecs.** POD structs for §3.3 in `content.h`; little-endian encode/decode
  through `eng::serialize` with replay-style cursor copy, count-vs-remaining guards, trailing-bytes
  check; `AssetId` references validated for shape (not existence) at decode.
- [ ] **S3 Cooker.** `--kind` per record; authored text → record → `.mba`; brute-force, byte-identical
  recook (`cooker_cli` test extended); generated id constants header for the authored set.
- [ ] **S4 Runtime.** `asset_load` switch + fail-closed validation; registry stores records in the
  level arena; sandbox loads one `HeroDef` and one `MapGrid` `.mba` alongside the texture.
- [ ] **S5 Goldens + malformed matrix.** One golden per kind under `assets/content/`, produced by an
  independent Python encoder (pattern: `tests/sim/map_golden.py`); malformed cases per §7.2 item 8;
  duplicate-id and bad-reference fixtures.
- [ ] **S6 Gates.** Full matrix, ASan, UBSan, `assets_boundary`, Vulkan smoke (sandbox changes);
  slate; ROADMAP; JOURNAL; PR.

## Acceptance

Proto-design §7.2 item 8 in full; oracle unchanged (no sim change); `asset_parsers` stays POD-C;
`assets_boundary` still rejects renderer/sim coupling.

## Held questions (proceed under the default)

| Question | Default |
|---|---|
| Per-kind schema versioning | `schema_version` u32 inside each payload; container version untouched |
| Id derivation for authored records | normalized path of the authored source file (ADR-0010), not a hand-typed id |
| Authored input format | one record per UTF-8 text file, `key = value` lines, cooker-only parser |
| Where the map payload comes from | wrap the `.mapdesc` bytes verbatim as `MbaType::MAP` (no re-encode) |
