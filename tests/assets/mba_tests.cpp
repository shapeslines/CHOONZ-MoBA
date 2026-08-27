#include "test.h"

#include "assets/mba.h"

#include <cstring>

static void mba_test_write_u32le(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

TEST(mba, texture_encoding_is_explicit_and_byte_deterministic) {
    const uint8_t rgba8[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    const MbaTextureSource source{rgba8, sizeof(rgba8), 2u, 1u};
    const AssetId id = asset_id("textures/pixel.mba");
    static_assert(asset_id("textures/pixel.mba") != ASSET_ID_NULL,
                  "canonical asset path must hash");

    size_t expected_bytes = 0u;
    CHECK(mba_texture_measure(&source, &expected_bytes));
    CHECK(expected_bytes == MBA_HEADER_BYTES + MBA_TEXTURE_PAYLOAD_HEADER_BYTES +
                                sizeof(rgba8));

    uint8_t first[64];
    uint8_t second[64];
    std::memset(first, 0xA5, sizeof(first));
    std::memset(second, 0x5A, sizeof(second));
    size_t first_bytes = 0u;
    size_t second_bytes = 0u;
    CHECK(mba_encode_texture(first, sizeof(first), id, &source, &first_bytes));
    CHECK(mba_encode_texture(second, sizeof(second), id, &source, &second_bytes));
    CHECK(first_bytes == expected_bytes && second_bytes == expected_bytes);
    CHECK(std::memcmp(first, second, expected_bytes) == 0);
    CHECK(first[0] == 'M' && first[1] == 'B' && first[2] == 'A' && first[3] == 0u);

    MbaAssetView view{};
    CHECK(mba_inspect(first, first_bytes, &view));
    CHECK(view.header.magic == MBA_MAGIC && view.header.version == MBA_VERSION);
    CHECK(view.header.type == (uint32_t)MBA_ASSET_TYPE_TEXTURE &&
          view.header.asset_id == id);
    CHECK(view.texture.width == 2u && view.texture.height == 1u &&
          view.texture.format == MBA_TEXTURE_FORMAT_RGBA8);
    CHECK(view.texture.rgba8_bytes == sizeof(rgba8));
    CHECK(std::memcmp(view.texture.rgba8, rgba8, sizeof(rgba8)) == 0);
}

TEST(mba, bad_magic_and_version_are_hard_rejected_without_publishing_a_view) {
    const uint8_t rgba8[] = {0x11u, 0x22u, 0x33u, 0x44u};
    const MbaTextureSource source{rgba8, sizeof(rgba8), 1u, 1u};
    uint8_t baked[64]{};
    size_t baked_bytes = 0u;
    CHECK(mba_encode_texture(baked, sizeof(baked), asset_id("fixture.mba"), &source,
                             &baked_bytes));

    MbaAssetView untouched;
    std::memset(&untouched, 0xA5, sizeof(untouched));
    const MbaAssetView before = untouched;
    baked[0] ^= 0x01u;
    CHECK(!mba_inspect(baked, baked_bytes, &untouched));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);

    baked[0] ^= 0x01u;
    mba_test_write_u32le(baked + 4u, MBA_VERSION + 1u);
    CHECK(!mba_inspect(baked, baked_bytes, &untouched));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);
}

TEST(mba, malformed_payload_length_is_rejected_without_writing_destination_on_encode_failure) {
    const uint8_t rgba8[] = {1u, 2u, 3u, 4u};
    const MbaTextureSource bad_source{rgba8, sizeof(rgba8) - 1u, 1u, 1u};
    uint8_t destination[64];
    std::memset(destination, 0xA5, sizeof(destination));
    uint8_t before[64];
    std::memcpy(before, destination, sizeof(before));
    size_t written = 999u;
    CHECK(!mba_encode_texture(destination, sizeof(destination), asset_id("bad.mba"),
                              &bad_source, &written));
    CHECK(std::memcmp(destination, before, sizeof(destination)) == 0);
    CHECK(written == 999u);
}
