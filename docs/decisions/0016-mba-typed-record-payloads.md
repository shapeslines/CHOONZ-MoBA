# ADR 0016 — Typed content record payloads in `.mba`

- **Status:** Accepted (2026-09-03)
- **Supersedes in part:** [0015](0015-mba-v1-container.md) (the "one texture payload"
  clause; the header and every rejection rule of 0015 stand unchanged)
- **See also:** [0009](0009-error-handling.md), [0010](0010-asset-id-scheme.md),
  `docs/slate-moba-proto-design.md` §3.3, `docs/plans/content-typed-payloads.md`

## Context

Phase 5 gameplay slices (`m5-hero-combat`, `m5-lane-objectives`) consume authored
content: hero definitions with nested actions and effects, lane objectives, and
economy rules (proto-design §3.3), plus the M5.0 map grid. The design rule is
"balance values live in cooked data, never in C++". `.mba` v1 accepts only a
texture payload, and the asset modules may not include `sim/` headers
(`tests/assets/check_*_boundary.cmake`), so a map record cannot be decoded into a
`MapGrid` inside the asset layer.

## Decision

`.mba` stays at container **version 1**; the 32-byte header and all of ADR-0015's
rejection rules are unchanged. Four typed **record** tags join `MbaAssetType`:
`HERO = 5`, `OBJECTIVE = 6`, `ECONOMY = 7`, `MAP = 8` (2–4 remain reserved for
mesh, sound, font).

A record payload is a 16-byte record header followed by the record body:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | `record_kind` (must equal the container type tag) |
| 4 | 4 | `schema_version` (1) |
| 8 | 4 | `record_bytes` (must equal `payload_bytes - 16`) |
| 12 | 4 | reserved, zero |

Record bodies are explicit little-endian encodings in the §3.3 field order,
written and read through `eng::serialize` (`engine/asset_parsers/include/assets/content.h`):
`HeroDef` nests `ActionDef[action_count]` which nest `EffectDef[effect_count]`;
`ObjectiveDef` and `EconomyRule` are flat. One `.mba` holds one top-level record;
sets of records are directories of files. A `MAP` body is the `.mapdesc` bytes
verbatim (`MAPD` magic, schema 1); the asset layer checks its header and declared
counts only and `eng::sim` decodes it.

`mba_inspect` validates a record payload **fully** before publishing a view: the
record header, and for HERO/OBJECTIVE/ECONOMY a decode through the content codec
that must consume exactly `record_bytes` and pass semantic validation (non-null
ids, known enums, zero reserved fields, counts within the fixed capacities, action
slots ascending from zero, non-negative magnitudes). `mba_encode_record` applies
the same validation, so the cooker cannot bake a record the runtime would reject.

The registry stores a record verbatim in the lifetime arena as `ASSET_TYPE_RECORD`
(`meta0 = kind`, `meta1 = schema_version`, `meta2 = bytes`) and exposes it through
`asset_get_record`. No renderer callback is involved.

The cooker gains `--kind texture|hero|objective|economy|map` (default `texture`).
Record kinds read a UTF-8 `key = value` authored text file (cooker-only parser;
ids are canonical asset paths hashed per ADR-0010); `map` copies validated bytes.

## Consequences

- Content is authorable offline, cooked byte-identically, loaded through the
  single `asset_load` path, and referenced by stable `AssetId`, with no sim change.
- `eng_asset_parsers` now links `eng::serialize` (boundary-legal; still POD-C, no
  STL); its hand-rolled little-endian helpers are gone.
- ADR-0010's generated `asset_ids.gen.h` and build-time collision check remain
  unpaid; they are a named follow-on slice, not part of this decision.
- Per-record schema evolution is carried by `schema_version` inside the payload,
  so the container version does not move for content changes.
