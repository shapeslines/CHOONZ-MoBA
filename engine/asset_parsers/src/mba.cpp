#include "assets/mba.h"

#include "serialize/byte_io.h"

static bool texture_layout(const MbaTextureSource* source, uint32_t* out_payload,
                           size_t* out_file) {
    if (!source || !out_payload || !out_file || source->width == 0u ||
        source->height == 0u) return false;
    const uint64_t pixels = (uint64_t)source->width * source->height;
    if (pixels > SIZE_MAX / 4u) return false;
    const size_t rgba_bytes = (size_t)pixels * 4u;
    if (source->rgba8_bytes != rgba_bytes ||
        rgba_bytes > (size_t)UINT32_MAX - MBA_TEXTURE_DATA_OFFSET) return false;
    const uint32_t payload = MBA_TEXTURE_DATA_OFFSET + (uint32_t)rgba_bytes;
    if ((size_t)payload > SIZE_MAX - MBA_HEADER_BYTES) return false;
    *out_payload = payload;
    *out_file = MBA_HEADER_BYTES + (size_t)payload;
    return true;
}

static bool sound_layout(const MbaSoundSource* source, uint32_t* out_frames,
                         uint32_t* out_payload, size_t* out_file) {
    if (!source || !out_frames || !out_payload || !out_file ||
        source->sample_rate == 0u || source->sample_rate > 384000u ||
        source->channels == 0u || source->channels > 2u ||
        (source->bits_per_sample != 8u && source->bits_per_sample != 16u &&
         source->bits_per_sample != 24u && source->bits_per_sample != 32u) ||
        source->pcm_bytes == 0u) return false;
    const uint32_t bytes_per_frame = (uint32_t)source->channels *
                                     ((uint32_t)source->bits_per_sample / 8u);
    if (bytes_per_frame == 0u || source->pcm_bytes % bytes_per_frame != 0u ||
        source->pcm_bytes / bytes_per_frame > UINT32_MAX ||
        source->pcm_bytes > (size_t)UINT32_MAX - MBA_SOUND_METADATA_BYTES)
        return false;
    const uint32_t payload = MBA_SOUND_METADATA_BYTES + (uint32_t)source->pcm_bytes;
    if ((size_t)payload > SIZE_MAX - MBA_HEADER_BYTES) return false;
    *out_frames = (uint32_t)(source->pcm_bytes / bytes_per_frame);
    *out_payload = payload;
    *out_file = MBA_HEADER_BYTES + (size_t)payload;
    return true;
}

bool mba_texture_measure(const MbaTextureSource* source, size_t* out_file_bytes) {
    if (!out_file_bytes) return false;
    uint32_t payload = 0u;
    size_t file_bytes = 0u;
    if (!texture_layout(source, &payload, &file_bytes)) return false;
    (void)payload;
    *out_file_bytes = file_bytes;
    return true;
}

bool mba_sound_measure(const MbaSoundSource* source, size_t* out_file_bytes) {
    if (!out_file_bytes) return false;
    uint32_t frames = 0u;
    uint32_t payload = 0u;
    size_t file_bytes = 0u;
    if (!sound_layout(source, &frames, &payload, &file_bytes)) return false;
    (void)frames;
    (void)payload;
    *out_file_bytes = file_bytes;
    return true;
}

static bool write_outer_header(ByteWriter* writer, AssetType type, uint32_t payload,
                               AssetId id) {
    return byte_writer_write_u32(writer, MBA_MAGIC) &&
           byte_writer_write_u32(writer, MBA_VERSION) &&
           byte_writer_write_u32(writer, (uint32_t)type) &&
           byte_writer_write_u32(writer, payload) &&
           byte_writer_write_u64(writer, id) &&
           byte_writer_write_u32(writer, 0u) &&
           byte_writer_write_u32(writer, 0u);
}

static bool writer_can_append(const ByteWriter* writer, size_t bytes) {
    return writer && writer->ok && writer->data &&
           writer->offset <= writer->capacity &&
           bytes <= writer->capacity - writer->offset;
}

bool mba_write_texture(ByteWriter* writer, AssetId id,
                       const MbaTextureSource* source) {
    uint32_t payload = 0u;
    size_t file_bytes = 0u;
    if (id == ASSET_ID_NULL || !source ||
        !source->rgba8 || !texture_layout(source, &payload, &file_bytes) ||
        !writer_can_append(writer, file_bytes)) return false;

    const uint32_t rgba_bytes = (uint32_t)source->rgba8_bytes;
    return write_outer_header(writer, ASSET_TYPE_TEXTURE, payload, id) &&
        byte_writer_write_u32(writer, source->width) &&
        byte_writer_write_u32(writer, source->height) &&
        byte_writer_write_u32(writer, MBA_TEXTURE_FORMAT_RGBA8) &&
        byte_writer_write_u32(writer, 1u) &&
        byte_writer_write_u32(writer, MBA_TEXTURE_METADATA_BYTES) &&
        byte_writer_write_u32(writer, MBA_TEXTURE_DATA_OFFSET) &&
        byte_writer_write_u32(writer, rgba_bytes) &&
        byte_writer_write_u32(writer, 0u) &&
        byte_writer_write_u32(writer, source->width) &&
        byte_writer_write_u32(writer, source->height) &&
        byte_writer_write_u32(writer, MBA_TEXTURE_DATA_OFFSET) &&
        byte_writer_write_u32(writer, rgba_bytes) &&
        byte_writer_write_bytes(writer, source->rgba8, source->rgba8_bytes);
}

bool mba_write_sound(ByteWriter* writer, AssetId id,
                     const MbaSoundSource* source) {
    uint32_t frames = 0u;
    uint32_t payload = 0u;
    size_t file_bytes = 0u;
    if (id == ASSET_ID_NULL || !source ||
        !source->pcm || !sound_layout(source, &frames, &payload, &file_bytes) ||
        !writer_can_append(writer, file_bytes)) return false;

    const uint32_t pcm_bytes = (uint32_t)source->pcm_bytes;
    return write_outer_header(writer, ASSET_TYPE_SOUND, payload, id) &&
        byte_writer_write_u32(writer, source->sample_rate) &&
        byte_writer_write_u32(writer, frames) &&
        byte_writer_write_u16(writer, source->channels) &&
        byte_writer_write_u16(writer, source->bits_per_sample) &&
        byte_writer_write_u32(writer, MBA_SOUND_ENCODING_PCM_INTEGER) &&
        byte_writer_write_u32(writer, MBA_SOUND_METADATA_BYTES) &&
        byte_writer_write_u32(writer, pcm_bytes) &&
        byte_writer_write_u32(writer, 0u) &&
        byte_writer_write_u32(writer, 0u) &&
        byte_writer_write_bytes(writer, source->pcm, source->pcm_bytes);
}

bool mba_encode_texture(void* destination, size_t capacity, AssetId id,
                        const MbaTextureSource* source, size_t* out_file_bytes) {
    size_t file_bytes = 0u;
    if (!destination || !out_file_bytes ||
        !mba_texture_measure(source, &file_bytes) || capacity < file_bytes)
        return false;
    ByteWriter writer{};
    byte_writer_init(&writer, destination, capacity);
    if (!mba_write_texture(&writer, id, source)) return false;
    *out_file_bytes = file_bytes;
    return true;
}

bool mba_encode_sound(void* destination, size_t capacity, AssetId id,
                      const MbaSoundSource* source, size_t* out_file_bytes) {
    size_t file_bytes = 0u;
    if (!destination || !out_file_bytes ||
        !mba_sound_measure(source, &file_bytes) || capacity < file_bytes)
        return false;
    ByteWriter writer{};
    byte_writer_init(&writer, destination, capacity);
    if (!mba_write_sound(&writer, id, source)) return false;
    *out_file_bytes = file_bytes;
    return true;
}

static bool inspect_texture(ByteReader* reader, uint32_t payload_bytes,
                            MbaTextureView* out) {
    uint32_t width = 0u, height = 0u, format = 0u, mip_count = 0u;
    uint32_t table_offset = 0u, data_offset = 0u, data_bytes = 0u, reserved = 0u;
    uint32_t mip_width = 0u, mip_height = 0u, mip_offset = 0u, mip_bytes = 0u;
    if (!byte_reader_read_u32(reader, &width) ||
        !byte_reader_read_u32(reader, &height) ||
        !byte_reader_read_u32(reader, &format) ||
        !byte_reader_read_u32(reader, &mip_count) ||
        !byte_reader_read_u32(reader, &table_offset) ||
        !byte_reader_read_u32(reader, &data_offset) ||
        !byte_reader_read_u32(reader, &data_bytes) ||
        !byte_reader_read_u32(reader, &reserved) ||
        !byte_reader_read_u32(reader, &mip_width) ||
        !byte_reader_read_u32(reader, &mip_height) ||
        !byte_reader_read_u32(reader, &mip_offset) ||
        !byte_reader_read_u32(reader, &mip_bytes)) return false;

    const uint64_t pixels = (uint64_t)width * height;
    if (width == 0u || height == 0u || pixels > UINT32_MAX / 4u ||
        format != MBA_TEXTURE_FORMAT_RGBA8 || mip_count != 1u || reserved != 0u ||
        table_offset != MBA_TEXTURE_METADATA_BYTES ||
        data_offset != MBA_TEXTURE_DATA_OFFSET || (data_offset & 15u) != 0u ||
        mip_width != width || mip_height != height || mip_offset != data_offset ||
        data_bytes != (uint32_t)(pixels * 4u) || mip_bytes != data_bytes ||
        payload_bytes != data_offset + data_bytes ||
        byte_reader_remaining(reader) != data_bytes) return false;

    MbaTextureView view{};
    view.rgba8 = reader->data + reader->offset;
    view.rgba8_bytes = data_bytes;
    view.width = width;
    view.height = height;
    view.format = format;
    view.mip_count = mip_count;
    view.data_offset = data_offset;
    *out = view;
    return true;
}

static bool inspect_sound(ByteReader* reader, uint32_t payload_bytes,
                          MbaSoundView* out) {
    uint32_t sample_rate = 0u, frame_count = 0u, encoding = 0u;
    uint32_t data_offset = 0u, data_bytes = 0u, reserved0 = 0u, reserved1 = 0u;
    uint16_t channels = 0u, bits = 0u;
    if (!byte_reader_read_u32(reader, &sample_rate) ||
        !byte_reader_read_u32(reader, &frame_count) ||
        !byte_reader_read_u16(reader, &channels) ||
        !byte_reader_read_u16(reader, &bits) ||
        !byte_reader_read_u32(reader, &encoding) ||
        !byte_reader_read_u32(reader, &data_offset) ||
        !byte_reader_read_u32(reader, &data_bytes) ||
        !byte_reader_read_u32(reader, &reserved0) ||
        !byte_reader_read_u32(reader, &reserved1)) return false;

    if (sample_rate == 0u || sample_rate > 384000u || frame_count == 0u ||
        channels == 0u || channels > 2u ||
        (bits != 8u && bits != 16u && bits != 24u && bits != 32u) ||
        encoding != MBA_SOUND_ENCODING_PCM_INTEGER ||
        data_offset != MBA_SOUND_METADATA_BYTES || reserved0 != 0u || reserved1 != 0u)
        return false;
    const uint32_t bytes_per_frame = (uint32_t)channels * ((uint32_t)bits / 8u);
    const uint64_t expected_bytes = (uint64_t)frame_count * bytes_per_frame;
    if (expected_bytes == 0u || expected_bytes > UINT32_MAX ||
        data_bytes != (uint32_t)expected_bytes ||
        payload_bytes != data_offset + data_bytes ||
        byte_reader_remaining(reader) != data_bytes) return false;

    MbaSoundView view{};
    view.pcm = reader->data + reader->offset;
    view.pcm_bytes = data_bytes;
    view.sample_rate = sample_rate;
    view.frame_count = frame_count;
    view.channels = channels;
    view.bits_per_sample = bits;
    view.encoding = encoding;
    view.data_offset = data_offset;
    *out = view;
    return true;
}

bool mba_inspect(const void* bytes, size_t size, MbaAssetView* out) {
    if (!bytes || !out || size < MBA_HEADER_BYTES) return false;
    ByteReader reader{};
    byte_reader_init(&reader, bytes, size);
    uint32_t magic = 0u, version = 0u, type = 0u, payload_bytes = 0u;
    uint32_t flags = 0u, reserved = 0u;
    uint64_t id = 0u;
    if (!byte_reader_read_u32(&reader, &magic) ||
        !byte_reader_read_u32(&reader, &version) ||
        !byte_reader_read_u32(&reader, &type) ||
        !byte_reader_read_u32(&reader, &payload_bytes) ||
        !byte_reader_read_u64(&reader, &id) ||
        !byte_reader_read_u32(&reader, &flags) ||
        !byte_reader_read_u32(&reader, &reserved) ||
        magic != MBA_MAGIC || version != MBA_VERSION || id == ASSET_ID_NULL ||
        flags != 0u || reserved != 0u || payload_bytes != size - MBA_HEADER_BYTES ||
        (type != ASSET_TYPE_TEXTURE && type != ASSET_TYPE_SOUND)) return false;

    MbaAssetView view{};
    view.id = id;
    view.type = (AssetType)type;
    view.payload_bytes = payload_bytes;
    if (view.type == ASSET_TYPE_TEXTURE) {
        if (!inspect_texture(&reader, payload_bytes, &view.texture)) return false;
    } else {
        if (!inspect_sound(&reader, payload_bytes, &view.sound)) return false;
    }
    *out = view;
    return true;
}
