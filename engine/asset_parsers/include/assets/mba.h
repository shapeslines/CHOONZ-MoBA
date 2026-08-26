#pragma once

#include <stddef.h>
#include <stdint.h>

#include "assets/asset_id.h"

// The bytes on disk are always explicitly little-endian. MbaHeader documents the
// fixed wire layout; the codec never writes or reads the native struct directly.
constexpr uint32_t MBA_MAGIC = 0x0041424Du; // literal bytes: 'M' 'B' 'A' '\0'
constexpr uint32_t MBA_VERSION = 1u;
constexpr uint32_t MBA_HEADER_BYTES = 32u;
constexpr uint32_t MBA_TEXTURE_PAYLOAD_HEADER_BYTES = 16u;

typedef enum MbaAssetType {
    MBA_ASSET_TYPE_NONE = 0,
    MBA_ASSET_TYPE_TEXTURE = 1,
    MBA_ASSET_TYPE_MESH = 2,
    MBA_ASSET_TYPE_SOUND = 3,
    MBA_ASSET_TYPE_FONT = 4,
} MbaAssetType;

typedef enum MbaTextureFormat {
    MBA_TEXTURE_FORMAT_NONE = 0,
    MBA_TEXTURE_FORMAT_RGBA8 = 1,
} MbaTextureFormat;

typedef struct MbaHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t payload_bytes;
    AssetId  asset_id;
    uint32_t flags;
    uint32_t reserved;
} MbaHeader;

static_assert(sizeof(MbaHeader) == MBA_HEADER_BYTES,
              "MbaHeader must remain a 32-byte wire-layout description");
static_assert(offsetof(MbaHeader, asset_id) == 16u,
              "MbaHeader asset id offset is part of the wire contract");

typedef struct MbaTextureSource {
    const uint8_t* rgba8;
    size_t         rgba8_bytes;
    uint32_t       width;
    uint32_t       height;
} MbaTextureSource;

typedef struct MbaTextureView {
    const uint8_t* rgba8;
    size_t         rgba8_bytes;
    uint32_t       width;
    uint32_t       height;
    uint32_t       format;
} MbaTextureView;

typedef struct MbaAssetView {
    MbaHeader      header;
    MbaTextureView texture;
} MbaAssetView;

// These allocation-free operations are the shared engine/tool interface. Failure
// leaves output arguments untouched. M4.1 emits and accepts only one RGBA8 mip.
bool mba_texture_measure(const MbaTextureSource* source, size_t* out_file_bytes);
bool mba_encode_texture(void* destination, size_t capacity, AssetId asset_id,
                        const MbaTextureSource* source, size_t* out_file_bytes);
bool mba_inspect(const void* bytes, size_t size, MbaAssetView* out);
