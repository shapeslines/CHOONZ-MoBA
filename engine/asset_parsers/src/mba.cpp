#include "assets/mba.h"

#include <string.h>

static uint32_t mba_read_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t mba_read_u64le(const uint8_t* bytes) {
    uint64_t result = 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
        result |= (uint64_t)bytes[i] << (i * 8u);
    return result;
}

static void mba_write_u32le(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static void mba_write_u64le(uint8_t* bytes, uint64_t value) {
    for (uint32_t i = 0u; i < 8u; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
}

static bool mba_texture_pixel_bytes(uint32_t width, uint32_t height,
                                    size_t* out_bytes) {
    if (!out_bytes || width == 0u || height == 0u) return false;
    const uint64_t pixels = (uint64_t)width * (uint64_t)height;
    if (pixels > SIZE_MAX / 4u) return false;
    *out_bytes = (size_t)pixels * 4u;
    return true;
}

bool mba_texture_measure(const MbaTextureSource* source, size_t* out_file_bytes) {
    if (!source || !out_file_bytes || !source->rgba8) return false;
    size_t pixel_bytes = 0u;
    if (!mba_texture_pixel_bytes(source->width, source->height, &pixel_bytes) ||
        source->rgba8_bytes != pixel_bytes ||
        pixel_bytes > UINT32_MAX - MBA_TEXTURE_PAYLOAD_HEADER_BYTES ||
        pixel_bytes > SIZE_MAX - MBA_TEXTURE_PAYLOAD_HEADER_BYTES ||
        (size_t)MBA_HEADER_BYTES >
            SIZE_MAX - MBA_TEXTURE_PAYLOAD_HEADER_BYTES - pixel_bytes)
        return false;
    *out_file_bytes = MBA_HEADER_BYTES + MBA_TEXTURE_PAYLOAD_HEADER_BYTES + pixel_bytes;
    return true;
}

bool mba_encode_texture(void* destination, size_t capacity, AssetId asset_id,
                        const MbaTextureSource* source, size_t* out_file_bytes) {
    if (!destination || !out_file_bytes || asset_id == ASSET_ID_NULL) return false;
    size_t file_bytes = 0u;
    if (!mba_texture_measure(source, &file_bytes) || capacity < file_bytes) return false;

    uint8_t* bytes = (uint8_t*)destination;
    const uint32_t payload_bytes = (uint32_t)(file_bytes - MBA_HEADER_BYTES);
    mba_write_u32le(bytes + 0u, MBA_MAGIC);
    mba_write_u32le(bytes + 4u, MBA_VERSION);
    mba_write_u32le(bytes + 8u, MBA_ASSET_TYPE_TEXTURE);
    mba_write_u32le(bytes + 12u, payload_bytes);
    mba_write_u64le(bytes + 16u, asset_id);
    mba_write_u32le(bytes + 24u, 0u);
    mba_write_u32le(bytes + 28u, 0u);

    uint8_t* payload = bytes + MBA_HEADER_BYTES;
    mba_write_u32le(payload + 0u, source->width);
    mba_write_u32le(payload + 4u, source->height);
    mba_write_u32le(payload + 8u, MBA_TEXTURE_FORMAT_RGBA8);
    mba_write_u32le(payload + 12u, (uint32_t)source->rgba8_bytes);
    memcpy(payload + MBA_TEXTURE_PAYLOAD_HEADER_BYTES, source->rgba8,
           source->rgba8_bytes);
    *out_file_bytes = file_bytes;
    return true;
}

bool mba_inspect(const void* bytes, size_t size, MbaAssetView* out) {
    if (!bytes || !out || size < MBA_HEADER_BYTES) return false;
    const uint8_t* data = (const uint8_t*)bytes;
    MbaHeader header{};
    header.magic = mba_read_u32le(data + 0u);
    header.version = mba_read_u32le(data + 4u);
    header.type = mba_read_u32le(data + 8u);
    header.payload_bytes = mba_read_u32le(data + 12u);
    header.asset_id = mba_read_u64le(data + 16u);
    header.flags = mba_read_u32le(data + 24u);
    header.reserved = mba_read_u32le(data + 28u);
    if (header.magic != MBA_MAGIC || header.version != MBA_VERSION ||
        header.asset_id == ASSET_ID_NULL || header.flags != 0u || header.reserved != 0u ||
        (size_t)header.payload_bytes > SIZE_MAX - MBA_HEADER_BYTES ||
        (size_t)header.payload_bytes > size - MBA_HEADER_BYTES ||
        size != MBA_HEADER_BYTES + (size_t)header.payload_bytes)
        return false;

    MbaAssetView next{};
    next.header = header;
    switch ((MbaAssetType)header.type) {
    case MBA_ASSET_TYPE_TEXTURE: {
        if (header.payload_bytes < MBA_TEXTURE_PAYLOAD_HEADER_BYTES) return false;
        const uint8_t* payload = data + MBA_HEADER_BYTES;
        const uint32_t width = mba_read_u32le(payload + 0u);
        const uint32_t height = mba_read_u32le(payload + 4u);
        const uint32_t format = mba_read_u32le(payload + 8u);
        const uint32_t data_bytes = mba_read_u32le(payload + 12u);
        size_t expected_bytes = 0u;
        if (format != (uint32_t)MBA_TEXTURE_FORMAT_RGBA8 ||
            !mba_texture_pixel_bytes(width, height, &expected_bytes) ||
            (size_t)data_bytes != expected_bytes ||
            data_bytes != header.payload_bytes - MBA_TEXTURE_PAYLOAD_HEADER_BYTES)
            return false;
        next.texture.rgba8 = payload + MBA_TEXTURE_PAYLOAD_HEADER_BYTES;
        next.texture.rgba8_bytes = data_bytes;
        next.texture.width = width;
        next.texture.height = height;
        next.texture.format = format;
        break;
    }
    default:
        return false;
    }

    *out = next;
    return true;
}
