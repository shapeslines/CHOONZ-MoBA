# ADR 0015 — Unified baked asset container `.mba` version 1

- **Status:** Accepted (2026-08-13)
- **See also:** [0008](0008-shader-build.md), [0010](0010-asset-id-scheme.md),
  `ARCHITECTURE.md` §11

## Context

M4.0 proved strict TGA/WAV parsing and asset lifetimes, but source-format parsing at
runtime creates two load paths and leaves future PNG/glTF complexity in the game.
M4.1 needs one deterministic cooker output whose runtime validation is small,
allocation-free, and independent of native C++ layout.

No `.mba` format has shipped, so the first live format starts at version 1 rather
than inheriting the architecture sketch's placeholder version.

## Decision

All cooked assets use explicit little-endian serialization. Native structs, padding,
pointers, allocator state, and compiler ABI never appear on disk.

The fixed 32-byte outer header is:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic bytes `MBA\0` (`0x0041424D` little-endian) |
| 4 | 4 | format version `1` |
| 8 | 4 | `AssetType` |
| 12 | 4 | payload byte count |
| 16 | 8 | `AssetId` |
| 24 | 4 | flags, zero in v1 |
| 28 | 4 | reserved, zero |

The file size must equal 32 plus the declared payload exactly. Unknown versions,
types, flags, reserved values, truncation, and trailing bytes fail closed.

### Texture payload

The payload begins with eight little-endian `uint32_t` values (32 bytes): width,
height, format, mip count, mip-table offset, data offset, data byte count, reserved.
It is followed by 16-byte mip descriptors `{width, height, data_offset, data_bytes}`.
Offsets are payload-relative. M4.1 emits and accepts exactly one RGBA8 mip; its table
starts at offset 32 and its pixels start at offset 48, which also places the file
data at a 16-byte-aligned offset. The descriptor remains extensible for M4.2 mips.

### Sound payload

The payload begins with a fixed 32-byte metadata block: sample rate, frame count,
channel count, bit depth, encoding, data offset, data bytes, and two reserved words.
M4.1 accepts only integer PCM, one or two channels, and 8/16/24/32-bit samples. PCM
starts at payload offset 32 and its exact byte count must equal
`frames * channels * bytes_per_sample`.

`eng_asset_parsers` owns source identity/parsers and the allocation-free container
codec, exposes only POD C-style interfaces, and links only `core + serialize`.
`eng_assets` owns registry/lifetime/platform loading and consumes that codec. The
offline cooker may use STL privately but exposes none across the shared boundary.

## Consequences

- Runtime loading has one small validation path regardless of source format.
- Cooker and runtime share the exact codec without sharing STL, OS, renderer, game,
  or simulation dependencies.
- A deterministic behavior/layout change bumps the container version deliberately.
- Raw SPIR-V remains loose and renderer-owned under ADR-0008.
- PNG, glTF, compression, packs, and incremental cooking remain later milestones.
