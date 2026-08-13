#include "test.h"

#include "assets/mba.h"

#include <cstdint>
#include <cstring>

static void put_u16le(uint8_t* bytes, size_t offset, uint16_t value) {
    bytes[offset + 0u] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put_u32le(uint8_t* bytes, size_t offset, uint32_t value) {
    bytes[offset + 0u] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static bool untouched_view(const MbaAssetView& view) {
    const uint8_t* bytes = (const uint8_t*)&view;
    for (size_t i = 0u; i < sizeof(view); ++i)
        if (bytes[i] != 0xa5u) return false;
    return true;
}

static bool all_bytes_are(const uint8_t* bytes, size_t size, uint8_t value) {
    for (size_t i = 0u; i < size; ++i)
        if (bytes[i] != value) return false;
    return true;
}

TEST(mba, texture_known_bytes_and_roundtrip) {
    const uint8_t rgba[] = {0x11u, 0x22u, 0x33u, 0x44u};
    const MbaTextureSource source{rgba, sizeof(rgba), 1u, 1u};
    size_t measured = 999u;
    CHECK(mba_texture_measure(&source, &measured));
    CHECK(measured == 84u);

    uint8_t bytes[84] = {};
    size_t written = 777u;
    CHECK(mba_encode_texture(bytes, sizeof(bytes), 0x0102030405060708ULL,
                             &source, &written));
    CHECK(written == sizeof(bytes));

    const uint8_t expected[84] = {
        0x4d,0x42,0x41,0x00, 0x01,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x34,0x00,0x00,0x00,
        0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x01,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x01,0x00,0x00,0x00,
        0x20,0x00,0x00,0x00, 0x30,0x00,0x00,0x00,
        0x04,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x01,0x00,0x00,0x00,
        0x30,0x00,0x00,0x00, 0x04,0x00,0x00,0x00,
        0x11,0x22,0x33,0x44,
    };
    CHECK(std::memcmp(bytes, expected, sizeof(bytes)) == 0);

    MbaAssetView view{};
    CHECK(mba_inspect(bytes, sizeof(bytes), &view));
    CHECK(view.id == 0x0102030405060708ULL);
    CHECK(view.type == ASSET_TYPE_TEXTURE);
    CHECK(view.payload_bytes == 52u);
    CHECK(view.texture.width == 1u && view.texture.height == 1u);
    CHECK(view.texture.format == MBA_TEXTURE_FORMAT_RGBA8);
    CHECK(view.texture.mip_count == 1u);
    CHECK(view.texture.data_offset == MBA_TEXTURE_DATA_OFFSET);
    CHECK(view.texture.rgba8_bytes == sizeof(rgba));
    CHECK(std::memcmp(view.texture.rgba8, rgba, sizeof(rgba)) == 0);
}

TEST(mba, sound_known_bytes_and_roundtrip) {
    const uint8_t pcm[] = {0x01u, 0x02u, 0x03u, 0x04u};
    const MbaSoundSource source{pcm, sizeof(pcm), 48000u, 2u, 16u};
    size_t measured = 999u;
    CHECK(mba_sound_measure(&source, &measured));
    CHECK(measured == 68u);

    uint8_t bytes[68] = {};
    size_t written = 777u;
    CHECK(mba_encode_sound(bytes, sizeof(bytes), 0x8877665544332211ULL,
                           &source, &written));
    CHECK(written == sizeof(bytes));

    const uint8_t expected[68] = {
        0x4d,0x42,0x41,0x00, 0x01,0x00,0x00,0x00,
        0x02,0x00,0x00,0x00, 0x24,0x00,0x00,0x00,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x80,0xbb,0x00,0x00, 0x01,0x00,0x00,0x00,
        0x02,0x00, 0x10,0x00, 0x01,0x00,0x00,0x00,
        0x20,0x00,0x00,0x00, 0x04,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x01,0x02,0x03,0x04,
    };
    CHECK(std::memcmp(bytes, expected, sizeof(bytes)) == 0);

    MbaAssetView view{};
    CHECK(mba_inspect(bytes, sizeof(bytes), &view));
    CHECK(view.id == 0x8877665544332211ULL);
    CHECK(view.type == ASSET_TYPE_SOUND);
    CHECK(view.payload_bytes == 36u);
    CHECK(view.sound.sample_rate == 48000u);
    CHECK(view.sound.frame_count == 1u);
    CHECK(view.sound.channels == 2u && view.sound.bits_per_sample == 16u);
    CHECK(view.sound.encoding == MBA_SOUND_ENCODING_PCM_INTEGER);
    CHECK(view.sound.data_offset == MBA_SOUND_METADATA_BYTES);
    CHECK(view.sound.pcm_bytes == sizeof(pcm));
    CHECK(std::memcmp(view.sound.pcm, pcm, sizeof(pcm)) == 0);
}

TEST(mba, encode_validation_and_capacity_are_atomic) {
    const uint8_t rgba[] = {1u, 2u, 3u, 4u};
    MbaTextureSource texture{rgba, sizeof(rgba), 1u, 1u};
    uint8_t bytes[84];
    std::memset(bytes, 0x5au, sizeof(bytes));
    size_t written = 0x1234u;
    CHECK(!mba_encode_texture(bytes, sizeof(bytes) - 1u, 1u, &texture, &written));
    CHECK(written == 0x1234u);
    CHECK(all_bytes_are(bytes, sizeof(bytes), 0x5au));

    uint8_t writer_bytes[100];
    std::memset(writer_bytes, 0x6bu, sizeof(writer_bytes));
    ByteWriter writer{};
    byte_writer_init(&writer, writer_bytes, sizeof(writer_bytes));
    CHECK(byte_writer_write_u32(&writer, 0x01020304u));
    const size_t saved_offset = writer.offset;
    const bool saved_ok = writer.ok;
    uint8_t suffix[96];
    std::memcpy(suffix, writer_bytes + saved_offset, sizeof(suffix));
    const MbaTextureSource valid_texture{rgba, sizeof(rgba), 1u, 1u};
    CHECK(!mba_write_texture(&writer, ASSET_ID_NULL, &valid_texture));
    CHECK(writer.offset == saved_offset && writer.ok == saved_ok);
    CHECK(std::memcmp(writer_bytes + saved_offset, suffix, sizeof(suffix)) == 0);

    writer.capacity = saved_offset + 83u;
    CHECK(!mba_write_texture(&writer, 1u, &valid_texture));
    CHECK(writer.offset == saved_offset && writer.ok == saved_ok);
    CHECK(std::memcmp(writer_bytes + saved_offset, suffix, sizeof(suffix)) == 0);

    size_t measured = 0x5678u;
    texture.rgba8_bytes = 3u;
    CHECK(!mba_texture_measure(&texture, &measured));
    CHECK(measured == 0x5678u);
    CHECK(!mba_encode_texture(bytes, sizeof(bytes), 1u, &texture, &written));
    CHECK(written == 0x1234u);
    CHECK(all_bytes_are(bytes, sizeof(bytes), 0x5au));

    const uint8_t pcm[] = {1u, 2u, 3u};
    const MbaSoundSource sound{pcm, sizeof(pcm), 48000u, 2u, 16u};
    CHECK(!mba_sound_measure(&sound, &measured));
    CHECK(measured == 0x5678u);
    CHECK(!mba_encode_sound(bytes, sizeof(bytes), 1u, &sound, &written));
    CHECK(written == 0x1234u);
    CHECK(all_bytes_are(bytes, sizeof(bytes), 0x5au));
}

static bool texture_mutation_rejected(size_t offset, uint32_t value) {
    const uint8_t rgba[] = {1u, 2u, 3u, 4u};
    const MbaTextureSource source{rgba, sizeof(rgba), 1u, 1u};
    uint8_t bytes[85] = {};
    size_t written = 0u;
    if (!mba_encode_texture(bytes, 84u, 0x1234u, &source, &written)) return false;
    put_u32le(bytes, offset, value);
    MbaAssetView out;
    std::memset(&out, 0xa5, sizeof(out));
    return !mba_inspect(bytes, written, &out) && untouched_view(out);
}

TEST(mba, malformed_texture_matrix_fails_without_output) {
    struct Mutation { size_t offset; uint32_t value; };
    const Mutation mutations[] = {
        {0u, 0u}, {4u, 2u}, {8u, 99u}, {12u, 51u}, {16u, 0u},
        {24u, 1u}, {28u, 1u},
        {32u, 0u}, {36u, 0u}, {40u, 99u}, {44u, 2u},
        {48u, 31u}, {52u, 47u}, {56u, 8u}, {60u, 1u},
        {64u, 2u}, {68u, 2u}, {72u, 32u}, {76u, 8u},
    };
    for (size_t i = 0u; i < sizeof(mutations) / sizeof(mutations[0]); ++i)
        CHECK(texture_mutation_rejected(mutations[i].offset, mutations[i].value));

    const uint8_t rgba[] = {1u, 2u, 3u, 4u};
    const MbaTextureSource source{rgba, sizeof(rgba), 1u, 1u};
    uint8_t bytes[85] = {};
    size_t written = 0u;
    CHECK(mba_encode_texture(bytes, 84u, 0x1234u, &source, &written));
    MbaAssetView out;
    std::memset(&out, 0xa5, sizeof(out));
    CHECK(!mba_inspect(bytes, written - 1u, &out));
    CHECK(untouched_view(out));
    std::memset(&out, 0xa5, sizeof(out));
    CHECK(!mba_inspect(bytes, written + 1u, &out));
    CHECK(untouched_view(out));
}

static bool sound_mutation_rejected(size_t offset, uint32_t value, bool halfword) {
    const uint8_t pcm[] = {1u, 2u, 3u, 4u};
    const MbaSoundSource source{pcm, sizeof(pcm), 48000u, 2u, 16u};
    uint8_t bytes[68] = {};
    size_t written = 0u;
    if (!mba_encode_sound(bytes, sizeof(bytes), 0x1234u, &source, &written)) return false;
    if (halfword) put_u16le(bytes, offset, (uint16_t)value);
    else put_u32le(bytes, offset, value);
    MbaAssetView out;
    std::memset(&out, 0xa5, sizeof(out));
    return !mba_inspect(bytes, written, &out) && untouched_view(out);
}

TEST(mba, malformed_sound_matrix_fails_without_output) {
    CHECK(sound_mutation_rejected(32u, 0u, false));
    CHECK(sound_mutation_rejected(36u, 0u, false));
    CHECK(sound_mutation_rejected(40u, 0u, true));
    CHECK(sound_mutation_rejected(40u, 3u, true));
    CHECK(sound_mutation_rejected(42u, 12u, true));
    CHECK(sound_mutation_rejected(44u, 99u, false));
    CHECK(sound_mutation_rejected(48u, 16u, false));
    CHECK(sound_mutation_rejected(52u, 8u, false));
    CHECK(sound_mutation_rejected(56u, 1u, false));
    CHECK(sound_mutation_rejected(60u, 1u, false));
}
