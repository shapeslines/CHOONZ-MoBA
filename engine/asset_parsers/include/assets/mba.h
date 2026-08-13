#pragma once

#include <stddef.h>
#include <stdint.h>

#include "assets/asset_catalog.h"
#include "serialize/byte_io.h"

constexpr uint32_t MBA_MAGIC = 0x0041424Du; // literal bytes: 'M' 'B' 'A' '\0'
constexpr uint32_t MBA_VERSION = 1u;
constexpr uint32_t MBA_HEADER_BYTES = 32u;
constexpr uint32_t MBA_TEXTURE_METADATA_BYTES = 32u;
constexpr uint32_t MBA_TEXTURE_MIP_DESCRIPTOR_BYTES = 16u;
constexpr uint32_t MBA_TEXTURE_DATA_OFFSET = 48u; // payload-relative, 16-byte aligned
constexpr uint32_t MBA_SOUND_METADATA_BYTES = 32u;

typedef enum MbaTextureFormat {
    MBA_TEXTURE_FORMAT_NONE = 0,
    MBA_TEXTURE_FORMAT_RGBA8 = 1,
} MbaTextureFormat;

typedef enum MbaSoundEncoding {
    MBA_SOUND_ENCODING_NONE = 0,
    MBA_SOUND_ENCODING_PCM_INTEGER = 1,
} MbaSoundEncoding;

typedef struct MbaTextureSource {
    const uint8_t* rgba8;
    size_t         rgba8_bytes;
    uint32_t       width;
    uint32_t       height;
} MbaTextureSource;

typedef struct MbaSoundSource {
    const uint8_t* pcm;
    size_t         pcm_bytes;
    uint32_t       sample_rate;
    uint16_t       channels;
    uint16_t       bits_per_sample;
} MbaSoundSource;

typedef struct MbaTextureView {
    const uint8_t* rgba8;
    size_t         rgba8_bytes;
    uint32_t       width;
    uint32_t       height;
    uint32_t       format;
    uint32_t       mip_count;
    uint32_t       data_offset;
} MbaTextureView;

typedef struct MbaSoundView {
    const uint8_t* pcm;
    size_t         pcm_bytes;
    uint32_t       sample_rate;
    uint32_t       frame_count;
    uint16_t       channels;
    uint16_t       bits_per_sample;
    uint32_t       encoding;
    uint32_t       data_offset;
} MbaSoundView;

typedef struct MbaAssetView {
    AssetId        id;
    AssetType      type;
    uint32_t       payload_bytes;
    MbaTextureView texture;
    MbaSoundView   sound;
} MbaAssetView;

// Measurement and encoding are allocation-free. Validation/capacity failure leaves
// the destination bytes and output size untouched. Native structs and padding are
// never serialized; every integer is written explicitly little-endian.
bool mba_texture_measure(const MbaTextureSource* source, size_t* out_file_bytes);
bool mba_sound_measure(const MbaSoundSource* source, size_t* out_file_bytes);
bool mba_write_texture(ByteWriter* writer, AssetId id,
                       const MbaTextureSource* source);
bool mba_write_sound(ByteWriter* writer, AssetId id,
                     const MbaSoundSource* source);
bool mba_encode_texture(void* destination, size_t capacity, AssetId id,
                        const MbaTextureSource* source, size_t* out_file_bytes);
bool mba_encode_sound(void* destination, size_t capacity, AssetId id,
                      const MbaSoundSource* source, size_t* out_file_bytes);

// Inspection validates the complete outer container and typed payload before
// publishing pointer views. Failure leaves `out` untouched.
bool mba_inspect(const void* bytes, size_t size, MbaAssetView* out);
