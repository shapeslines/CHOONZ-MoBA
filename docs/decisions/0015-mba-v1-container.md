# ADR 0015 — Unified baked asset container `.mba` version 1

- **Status:** Accepted (2026-08-25)
- **Superseded in part by:** [0016](0016-mba-typed-record-payloads.md) (2026-09-03) — the
  "one texture payload" clause; typed record payloads now share this container.
- **See also:** [0008](0008-shader-build.md), [0009](0009-error-handling.md),
  [0010](0010-asset-id-scheme.md), `ARCHITECTURE.md` §11

## Context

M4.0 proved strict direct TGA/WAV loading and registry lifetime rules, but source
format parsing does not scale to PNG or glTF in the runtime. M4.1 needs one
deterministic format whose inspection is small, allocation-free, and independent
of native C++ layout.

## Decision

`.mba` version 1 uses explicit little-endian serialization. Native structs,
padding, pointers, allocator state, and compiler ABI are never written to disk.

Every file starts with the fixed 32-byte `MbaHeader`:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic bytes `MBA\0` (`0x0041424D` little-endian) |
| 4 | 4 | format version `1` |
| 8 | 4 | `MbaAssetType` tag |
| 12 | 4 | payload byte count |
| 16 | 8 | `AssetId` |
| 24 | 4 | flags, zero in v1 |
| 28 | 4 | reserved, zero in v1 |

M4.1 emits and accepts one texture payload: a 16-byte block of `width`,
`height`, `RGBA8` format, and pixel byte count, followed by exactly one tightly
packed RGBA8 image. A cooked `.mba` is addressed by the same canonical relative
path whose `AssetId` is stored in its header; the runtime rejects an id mismatch.

`asset_load` is the only baked-runtime entry point. It hard-rejects bad magic,
unknown versions, unknown type tags, nonzero v1 flags/reserved fields, malformed
payloads, and trailing bytes before the registry or renderer sees data.

`eng_asset_parsers` owns the TGA parser, stable identity helpers, and `.mba`
codec. Its public interface is C-style POD only and it is compiled with the
engine's no-exception flags. `cooker.exe` uses STL only privately and performs a
brute-force TGA-to-`.mba` bake; it has no incremental/dependency-graph behavior.

## Consequences

- Runtime loading has one type-tagged, fail-closed baked path.
- Repeating a bake of identical source pixels and logical asset path produces
  byte-identical output.
- PNG/inflate, glTF, multi-mip generation, hot reload, pack files, generated
  catalogs, and incremental cooking remain deferred to their named milestones.
- Loose raw SPIR-V remains renderer-owned under ADR-0008.
