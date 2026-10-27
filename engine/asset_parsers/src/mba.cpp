#include "assets/mba.h"

#include <string.h>

#include "assets/content.h"
#include "serialize/byte_io.h"

// ---------------------------------------------------------------- shared helpers

static bool mba_texture_pixel_bytes(uint32_t width, uint32_t height,
                                    size_t* out_bytes) {
    if (!out_bytes || width == 0u || height == 0u) return false;
    const uint64_t pixels = (uint64_t)width * (uint64_t)height;
    if (pixels > SIZE_MAX / 4u) return false;
    *out_bytes = (size_t)pixels * 4u;
    return true;
}

static void mba_write_header(ByteWriter* w, uint32_t type, uint32_t payload_bytes,
                             AssetId asset_id) {
    byte_writer_write_u32(w, MBA_MAGIC);
    byte_writer_write_u32(w, MBA_VERSION);
    byte_writer_write_u32(w, type);
    byte_writer_write_u32(w, payload_bytes);
    byte_writer_write_u64(w, asset_id);
    byte_writer_write_u32(w, 0u);
    byte_writer_write_u32(w, 0u);
}

// ---------------------------------------------------------------- textures

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

    ByteWriter w;
    byte_writer_init(&w, destination, file_bytes);
    mba_write_header(&w, MBA_ASSET_TYPE_TEXTURE, (uint32_t)(file_bytes - MBA_HEADER_BYTES), asset_id);
    byte_writer_write_u32(&w, source->width);
    byte_writer_write_u32(&w, source->height);
    byte_writer_write_u32(&w, MBA_TEXTURE_FORMAT_RGBA8);
    byte_writer_write_u32(&w, (uint32_t)source->rgba8_bytes);
    byte_writer_write_bytes(&w, source->rgba8, source->rgba8_bytes);
    if (!byte_writer_ok(&w) || byte_writer_size(&w) != file_bytes) return false;
    *out_file_bytes = file_bytes;
    return true;
}

// ---------------------------------------------------------------- records

bool mba_record_is_kind(uint32_t type) {
    return type == MBA_ASSET_TYPE_HERO || type == MBA_ASSET_TYPE_OBJECTIVE ||
           type == MBA_ASSET_TYPE_ECONOMY || type == MBA_ASSET_TYPE_MAP;
}

// A record body is valid when its content codec accepts it and consumes exactly
// the declared bytes (map bodies are opaque and only header-checked).
static bool mba_record_body_valid(uint32_t kind, const uint8_t* body, size_t size,
                                  uint32_t schema_version) {
    if (!body || size == 0u || size > MBA_RECORD_MAX_BYTES) return false;
    if (schema_version != CONTENT_SCHEMA_VERSION) return false;
    ByteReader r;
    byte_reader_init(&r, body, size);
    switch ((MbaAssetType)kind) {
    case MBA_ASSET_TYPE_HERO: {
        HeroDef h{};
        return content_decode_hero(&r, &h) && byte_reader_remaining(&r) == 0u &&
               h.schema_version == schema_version;
    }
    case MBA_ASSET_TYPE_OBJECTIVE: {
        ObjectiveDef o{};
        return content_decode_objective(&r, &o) && byte_reader_remaining(&r) == 0u;
    }
    case MBA_ASSET_TYPE_ECONOMY: {
        EconomyRule e{};
        return content_decode_economy(&r, &e) && byte_reader_remaining(&r) == 0u;
    }
    case MBA_ASSET_TYPE_MAP:
        return content_map_bytes_valid(body, size);
    default:
        return false;
    }
}

bool mba_record_measure(uint32_t kind, const uint8_t* record_bytes, size_t record_size,
                        size_t* out_file_bytes) {
    if (!out_file_bytes || !mba_record_is_kind(kind)) return false;
    if (!mba_record_body_valid(kind, record_bytes, record_size, CONTENT_SCHEMA_VERSION)) return false;
    *out_file_bytes = MBA_HEADER_BYTES + MBA_RECORD_PAYLOAD_HEADER_BYTES + record_size;
    return true;
}

bool mba_encode_record(void* destination, size_t capacity, AssetId asset_id, uint32_t kind,
                       uint32_t schema_version, const uint8_t* record_bytes, size_t record_size,
                       size_t* out_file_bytes) {
    if (!destination || !out_file_bytes || asset_id == ASSET_ID_NULL) return false;
    if (schema_version != CONTENT_SCHEMA_VERSION) return false;
    size_t file_bytes = 0u;
    if (!mba_record_measure(kind, record_bytes, record_size, &file_bytes) || capacity < file_bytes)
        return false;

    ByteWriter w;
    byte_writer_init(&w, destination, file_bytes);
    mba_write_header(&w, kind, (uint32_t)(file_bytes - MBA_HEADER_BYTES), asset_id);
    byte_writer_write_u32(&w, kind);
    byte_writer_write_u32(&w, schema_version);
    byte_writer_write_u32(&w, (uint32_t)record_size);
    byte_writer_write_u32(&w, 0u);
    byte_writer_write_bytes(&w, record_bytes, record_size);
    if (!byte_writer_ok(&w) || byte_writer_size(&w) != file_bytes) return false;
    *out_file_bytes = file_bytes;
    return true;
}

// ---------------------------------------------------------------- inspect

bool mba_inspect(const void* bytes, size_t size, MbaAssetView* out) {
    if (!bytes || !out || size < MBA_HEADER_BYTES) return false;
    const uint8_t* data = (const uint8_t*)bytes;
    ByteReader r;
    byte_reader_init(&r, data, size);
    MbaHeader header{};
    if (!byte_reader_read_u32(&r, &header.magic) || !byte_reader_read_u32(&r, &header.version) ||
        !byte_reader_read_u32(&r, &header.type) || !byte_reader_read_u32(&r, &header.payload_bytes) ||
        !byte_reader_read_u64(&r, &header.asset_id) || !byte_reader_read_u32(&r, &header.flags) ||
        !byte_reader_read_u32(&r, &header.reserved)) return false;
    if (header.magic != MBA_MAGIC || header.version != MBA_VERSION ||
        header.asset_id == ASSET_ID_NULL || header.flags != 0u || header.reserved != 0u ||
        (size_t)header.payload_bytes > SIZE_MAX - MBA_HEADER_BYTES ||
        (size_t)header.payload_bytes > size - MBA_HEADER_BYTES ||
        size != MBA_HEADER_BYTES + (size_t)header.payload_bytes)
        return false;

    MbaAssetView next{};
    next.header = header;
    const uint8_t* payload = data + MBA_HEADER_BYTES;
    switch ((MbaAssetType)header.type) {
    case MBA_ASSET_TYPE_TEXTURE: {
        if (header.payload_bytes < MBA_TEXTURE_PAYLOAD_HEADER_BYTES) return false;
        uint32_t width = 0u, height = 0u, format = 0u, data_bytes = 0u;
        if (!byte_reader_read_u32(&r, &width) || !byte_reader_read_u32(&r, &height) ||
            !byte_reader_read_u32(&r, &format) || !byte_reader_read_u32(&r, &data_bytes)) return false;
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
    case MBA_ASSET_TYPE_HERO:
    case MBA_ASSET_TYPE_OBJECTIVE:
    case MBA_ASSET_TYPE_ECONOMY:
    case MBA_ASSET_TYPE_MAP: {
        if (header.payload_bytes < MBA_RECORD_PAYLOAD_HEADER_BYTES) return false;
        uint32_t record_kind = 0u, schema_version = 0u, record_bytes = 0u, reserved = 0u;
        if (!byte_reader_read_u32(&r, &record_kind) || !byte_reader_read_u32(&r, &schema_version) ||
            !byte_reader_read_u32(&r, &record_bytes) || !byte_reader_read_u32(&r, &reserved)) return false;
        if (record_kind != header.type || reserved != 0u ||
            record_bytes != header.payload_bytes - MBA_RECORD_PAYLOAD_HEADER_BYTES) return false;
        const uint8_t* body = payload + MBA_RECORD_PAYLOAD_HEADER_BYTES;
        if (!mba_record_body_valid(record_kind, body, record_bytes, schema_version)) return false;
        next.record.bytes = body;
        next.record.bytes_count = record_bytes;
        next.record.record_kind = record_kind;
        next.record.schema_version = schema_version;
        break;
    }
    default:
        return false;
    }

    *out = next;
    return true;
}
