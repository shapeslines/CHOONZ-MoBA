#include "test.h"

#include "assets/assets.h"
#include "assets/mba.h"
#include "platform/platform.h"
#include "win32_test_directory.h"

#include <cstdio>
#include <cstring>

struct FakeTextures {
    uint32_t creates;
    uint32_t destroys;
    uint32_t last_frames;
    Handle next;
};

static TextureHandle fake_create_texture(void* user, const uint8_t* pixels,
                                         uint32_t width, uint32_t height) {
    FakeTextures* fake = (FakeTextures*)user;
    TextureHandle result{HANDLE_NULL};
    if (!fake || !pixels || width == 0u || height == 0u) return result;
    ++fake->creates;
    result.h = fake->next++;
    if (handle_gen(result.h) == 0u) result.h = handle_make(fake->creates, 1u);
    return result;
}

static void fake_destroy_texture(void* user, TextureHandle texture,
                                 uint32_t frames_until_free) {
    FakeTextures* fake = (FakeTextures*)user;
    if (!fake || handle_is_null(texture.h)) return;
    ++fake->destroys;
    fake->last_frames = frames_until_free;
}

struct RegistryFixture {
    alignas(16) uint8_t persistent_storage[65536];
    alignas(16) uint8_t level_storage[4096];
    alignas(16) uint8_t global_storage[4096];
    alignas(16) uint8_t io_storage[4096];
    Arena persistent;
    Arena level;
    Arena global;
    Arena io;
    AssetRegistry registry;
    FakeTextures fake;
};

static bool init_fixture(RegistryFixture* fixture, uint32_t capacity,
                         const char* asset_root = ".") {
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->persistent, fixture->persistent_storage,
                     sizeof(fixture->persistent_storage));
    arena_init_fixed(&fixture->level, fixture->level_storage,
                     sizeof(fixture->level_storage));
    arena_init_fixed(&fixture->global, fixture->global_storage,
                     sizeof(fixture->global_storage));
    arena_init_fixed(&fixture->io, fixture->io_storage,
                     sizeof(fixture->io_storage));
    fixture->fake.next = handle_make(100u, 1u);
    AssetRendererApi renderer{&fixture->fake, fake_create_texture, fake_destroy_texture};
    AssetRegistryConfig config = asset_registry_config_default(asset_root);
    config.capacity = capacity;
    return asset_registry_init(&fixture->registry, &fixture->persistent, &fixture->level,
                               &fixture->global, &fixture->io, renderer, config);
}

TEST(assets, initialization_is_transactional_when_persistent_budget_is_short) {
    AssetRegistryConfig config = asset_registry_config_default("assets");
    config.capacity = 4u;
    const size_t required = asset_registry_memory_required(config);
    CHECK(required > 1u && required < 65536u);

    alignas(16) uint8_t storage[65536];
    alignas(16) uint8_t level_storage[64];
    alignas(16) uint8_t global_storage[64];
    alignas(16) uint8_t io_storage[64];
    Arena persistent, level, global, io;
    arena_init_fixed(&persistent, storage, required - 1u);
    arena_init_fixed(&level, level_storage, sizeof(level_storage));
    arena_init_fixed(&global, global_storage, sizeof(global_storage));
    arena_init_fixed(&io, io_storage, sizeof(io_storage));
    AssetRegistry registry;
    std::memset(&registry, 0xA5, sizeof(registry));
    AssetRegistry before = registry;
    const size_t offset = persistent.offset;
    AssetRendererApi renderer{};
    CHECK(!asset_registry_init(&registry, &persistent, &level, &global, &io,
                               renderer, config));
    CHECK(std::memcmp(&registry, &before, sizeof(registry)) == 0);
    CHECK(persistent.offset == offset);
}

TEST(assets, initialization_zeroes_nonzero_registry_backing) {
    alignas(16) uint8_t persistent_storage[65536];
    alignas(16) uint8_t level_storage[256];
    alignas(16) uint8_t global_storage[256];
    alignas(16) uint8_t io_storage[256];
    std::memset(persistent_storage, 0xA5, sizeof(persistent_storage));
    std::memset(level_storage, 0xA5, sizeof(level_storage));
    std::memset(global_storage, 0xA5, sizeof(global_storage));
    std::memset(io_storage, 0xA5, sizeof(io_storage));

    Arena persistent, level, global, io;
    arena_init_fixed(&persistent, persistent_storage, sizeof(persistent_storage));
    arena_init_fixed(&level, level_storage, sizeof(level_storage));
    arena_init_fixed(&global, global_storage, sizeof(global_storage));
    arena_init_fixed(&io, io_storage, sizeof(io_storage));
    AssetRegistry registry;
    std::memset(&registry, 0xA5, sizeof(registry));
    AssetRegistryConfig config = asset_registry_config_default(".");
    config.capacity = 2u;
    AssetRendererApi renderer{};
    CHECK(asset_registry_init(&registry, &persistent, &level, &global, &io,
                              renderer, config));

    const uint8_t pcm[4] = {1u, 2u, 3u, 4u};
    AssetSoundSource source{pcm, sizeof(pcm), 48000u, 1u, 8u};
    AssetHandle sound = asset_register_sound(&registry, "audio/nonzero.wav",
                                              ASSET_LIFETIME_LEVEL, source);
    CHECK(!handle_is_null(sound.h));
    CHECK(handle_index(sound.h) == 0u && handle_gen(sound.h) == 1u);
    CHECK(registry.live_count == 1u && registry.by_id.count == 1u);
    AssetSoundView view{};
    CHECK(asset_get_sound(&registry, sound, &view));
    CHECK(view.pcm_bytes == sizeof(pcm));
    CHECK(std::memcmp(view.pcm, pcm, sizeof(pcm)) == 0);
}

TEST(assets, initialization_rejects_overlapping_backing_without_mutation) {
    alignas(16) uint8_t shared[65536];
    alignas(16) uint8_t global_storage[256];
    alignas(16) uint8_t io_storage[256];
    Arena persistent, level, global, io;
    arena_init_fixed(&persistent, shared, sizeof(shared));
    arena_init_fixed(&level, shared, sizeof(shared));
    arena_init_fixed(&global, global_storage, sizeof(global_storage));
    arena_init_fixed(&io, io_storage, sizeof(io_storage));
    AssetRegistry registry;
    std::memset(&registry, 0xA5, sizeof(registry));
    const AssetRegistry before = registry;
    AssetRegistryConfig config = asset_registry_config_default(".");
    config.capacity = 2u;
    const size_t offsets[4] = {persistent.offset, level.offset, global.offset, io.offset};
    AssetRendererApi renderer{};
    CHECK(!asset_registry_init(&registry, &persistent, &level, &global, &io,
                               renderer, config));
    CHECK(std::memcmp(&registry, &before, sizeof(registry)) == 0);
    CHECK(persistent.offset == offsets[0] && level.offset == offsets[1] &&
          global.offset == offsets[2] && io.offset == offsets[3]);
}

TEST(assets, initialization_accepts_exactly_adjacent_backing_ranges) {
    alignas(16) uint8_t storage[65536 + 256 * 3];
    Arena persistent, level, global, io;
    arena_init_fixed(&persistent, storage, 65536u);
    arena_init_fixed(&level, storage + 65536u, 256u);
    arena_init_fixed(&global, storage + 65536u + 256u, 256u);
    arena_init_fixed(&io, storage + 65536u + 512u, 256u);
    AssetRegistry registry{};
    AssetRegistryConfig config = asset_registry_config_default(".");
    config.capacity = 2u;
    AssetRendererApi renderer{};
    CHECK(asset_registry_init(&registry, &persistent, &level, &global, &io,
                              renderer, config));
    CHECK(registry.initialized == 1u);
}

TEST(assets, initialization_rejects_registry_inside_arena_backing) {
    alignas(AssetRegistry) uint8_t persistent_storage[65536];
    alignas(16) uint8_t level_storage[256];
    alignas(16) uint8_t global_storage[256];
    alignas(16) uint8_t io_storage[256];
    Arena persistent, level, global, io;
    arena_init_fixed(&persistent, persistent_storage, sizeof(persistent_storage));
    arena_init_fixed(&level, level_storage, sizeof(level_storage));
    arena_init_fixed(&global, global_storage, sizeof(global_storage));
    arena_init_fixed(&io, io_storage, sizeof(io_storage));
    AssetRegistry* registry = reinterpret_cast<AssetRegistry*>(persistent_storage);
    std::memset(registry, 0xA5, sizeof(*registry));
    const AssetRegistry before = *registry;
    AssetRegistryConfig config = asset_registry_config_default(".");
    config.capacity = 2u;
    AssetRendererApi renderer{};
    CHECK(!asset_registry_init(registry, &persistent, &level, &global, &io,
                               renderer, config));
    CHECK(std::memcmp(registry, &before, sizeof(*registry)) == 0);
    CHECK(persistent.offset == 0u && level.offset == 0u &&
          global.offset == 0u && io.offset == 0u);
}

TEST(assets, windows_path_aliases_fail_without_registry_or_renderer_mutation) {
    RegistryFixture fixture;
    CHECK(init_fixture(&fixture, 4u));
    const uint8_t pixel[4] = {1u, 2u, 3u, 4u};
    const AssetTextureSource source{pixel, 1u, 1u};
    const char* aliases[] = {
        "unit.tga.", "unit.tga ", "con.tga", "dir/aux.wav", "dir/com1.bin",
    };
    const size_t level_offset = fixture.level.offset;
    for (size_t i = 0u; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        const AssetHandle rejected = asset_register_texture(
            &fixture.registry, aliases[i], ASSET_LIFETIME_LEVEL, source);
        CHECK(handle_is_null(rejected.h));
        CHECK(fixture.fake.creates == 0u && fixture.fake.destroys == 0u);
        CHECK(fixture.registry.live_count == 0u && fixture.registry.by_id.count == 0u);
        CHECK(fixture.level.offset == level_offset);
    }
}

TEST(assets, level_texture_roundtrips_and_unload_invalidates_handle) {
    RegistryFixture fixture;
    CHECK(init_fixture(&fixture, 2u));
    const uint8_t pixels[8] = {1,2,3,4, 5,6,7,8};
    AssetTextureSource source{pixels, 2u, 1u};
    AssetHandle texture = asset_register_texture(&fixture.registry,
                                                  "Textures\\Unit.TGA",
                                                  ASSET_LIFETIME_LEVEL, source);
    CHECK(!handle_is_null(texture.h));
    CHECK(fixture.fake.creates == 1u);
    CHECK(fixture.registry.live_count == 1u);

    AssetTextureView view{};
    CHECK(asset_get_texture(&fixture.registry, texture, &view));
    CHECK(view.id == asset_id("textures/unit.tga"));
    CHECK(view.width == 2u && view.height == 1u && view.rgba8_bytes == sizeof(pixels));
    CHECK(std::memcmp(view.rgba8, pixels, sizeof(pixels)) == 0);
    CHECK(!handle_is_null(view.gpu.h));
    CHECK(asset_registry_find_path(&fixture.registry, "textures/unit.tga").h == texture.h);

    AssetHandle duplicate = asset_register_texture(&fixture.registry,
                                                    "textures/./unit.tga",
                                                    ASSET_LIFETIME_LEVEL, source);
    CHECK(duplicate.h == texture.h);
    CHECK(fixture.fake.creates == 1u);

    CHECK(fixture.level.offset > 0u);
    assets_unload_level(&fixture.registry, 2u);
    CHECK(fixture.fake.destroys == 1u && fixture.fake.last_frames == 2u);
    CHECK(fixture.level.offset == fixture.registry.level_base_offset);
    CHECK(fixture.registry.live_count == 0u);
    CHECK(!asset_registry_valid(&fixture.registry, texture));
    CHECK(!asset_get_texture(&fixture.registry, texture, &view));

    AssetHandle replacement = asset_register_texture(&fixture.registry,
                                                      "textures/replacement.tga",
                                                      ASSET_LIFETIME_LEVEL, source);
    CHECK(!handle_is_null(replacement.h));
    CHECK(handle_index(replacement.h) == handle_index(texture.h));
    CHECK(handle_gen(replacement.h) != handle_gen(texture.h));
}

TEST(assets, global_refcounts_release_at_zero_and_survive_level_unload) {
    RegistryFixture fixture;
    CHECK(init_fixture(&fixture, 3u));
    const uint8_t pixels[4] = {9,8,7,6};
    AssetTextureSource source{pixels, 1u, 1u};
    AssetHandle global = asset_register_texture(&fixture.registry, "ui/global.tga",
                                                 ASSET_LIFETIME_GLOBAL, source);
    AssetHandle again = asset_register_texture(&fixture.registry, "UI\\GLOBAL.TGA",
                                                ASSET_LIFETIME_GLOBAL, source);
    CHECK(global.h == again.h);
    CHECK(asset_global_refcount(&fixture.registry, global) == 2u);
    CHECK(fixture.fake.creates == 1u);

    assets_unload_level(&fixture.registry, 1u);
    CHECK(asset_registry_valid(&fixture.registry, global));
    CHECK(fixture.fake.destroys == 0u);
    CHECK(asset_release_global(&fixture.registry, global, 3u));
    CHECK(asset_global_refcount(&fixture.registry, global) == 1u);
    CHECK(fixture.fake.destroys == 0u);
    CHECK(asset_release_global(&fixture.registry, global, 3u));
    CHECK(fixture.fake.destroys == 1u && fixture.fake.last_frames == 3u);
    CHECK(!asset_registry_valid(&fixture.registry, global));
    CHECK(!asset_release_global(&fixture.registry, global, 3u));
}

TEST(assets, sound_roundtrip_and_capacity_failure_do_not_mutate) {
    RegistryFixture fixture;
    CHECK(init_fixture(&fixture, 1u));
    const uint8_t pcm[8] = {0,1,2,3,4,5,6,7};
    AssetSoundSource source{pcm, sizeof(pcm), 48000u, 2u, 16u};
    AssetHandle sound = asset_register_sound(&fixture.registry, "audio/test.wav",
                                              ASSET_LIFETIME_LEVEL, source);
    CHECK(!handle_is_null(sound.h));
    AssetSoundView view{};
    CHECK(asset_get_sound(&fixture.registry, sound, &view));
    CHECK(view.sample_rate == 48000u && view.channels == 2u &&
          view.bits_per_sample == 16u && view.frame_count == 2u);
    CHECK(std::memcmp(view.pcm, pcm, sizeof(pcm)) == 0);

    const size_t level_offset = fixture.level.offset;
    const uint32_t live_count = fixture.registry.live_count;
    const uint32_t creates = fixture.fake.creates;
    const uint8_t pixel[4] = {1,1,1,1};
    AssetTextureSource texture_source{pixel, 1u, 1u};
    AssetHandle full = asset_register_texture(&fixture.registry, "full.tga",
                                               ASSET_LIFETIME_LEVEL, texture_source);
    CHECK(handle_is_null(full.h));
    CHECK(fixture.level.offset == level_offset);
    CHECK(fixture.registry.live_count == live_count);
    CHECK(fixture.fake.creates == creates);

    AssetHandle wrong_type = asset_register_texture(&fixture.registry, "audio/test.wav",
                                                     ASSET_LIFETIME_LEVEL, texture_source);
    CHECK(handle_is_null(wrong_type.h));
    CHECK(fixture.fake.creates == creates);
}

static void test_put_u16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
}

static void test_put_u32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

TEST(assets, loose_tga_and_wav_files_load_transactionally) {
    OwnedTestDirectory owned{};
    owned.scope_custody = INVALID_HANDLE_VALUE;
    owned.directory_custody = INVALID_HANDLE_VALUE;
    const bool directory_owned = test_create_owned_directory("assets", &owned);
    CHECK(directory_owned);
    if (!directory_owned) return;

    const char* tga_asset_path = "fixture.tga";
    const char* wav_asset_path = "fixture.wav";
    char tga_file_path[256];
    char wav_file_path[256];
    const int tga_chars = std::snprintf(
        tga_file_path, sizeof(tga_file_path), "%s/%s", owned.path, tga_asset_path);
    const int wav_chars = std::snprintf(
        wav_file_path, sizeof(wav_file_path), "%s/%s", owned.path, wav_asset_path);
    const bool paths_ready = tga_chars > 0 && (size_t)tga_chars < sizeof(tga_file_path) &&
        wav_chars > 0 && (size_t)wav_chars < sizeof(wav_file_path);
    CHECK(paths_ready);
    if (!paths_ready) {
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    uint8_t tga[22] = {};
    tga[2] = 2u;
    tga[12] = 1u; tga[14] = 1u;
    tga[16] = 32u; tga[17] = 0x28u;
    tga[18] = 0x11u; tga[19] = 0x22u; tga[20] = 0x33u; tga[21] = 0x44u;
    const bool tga_written = platform_file_write(tga_file_path, tga, sizeof(tga));
    CHECK(tga_written);

    const uint8_t pcm[] = {1u,0u, 2u,0u};
    uint8_t wav[48] = {};
    std::memcpy(wav + 0u, "RIFF", 4u);
    test_put_u32(wav + 4u, 40u);
    std::memcpy(wav + 8u, "WAVEfmt ", 8u);
    test_put_u32(wav + 16u, 16u);
    test_put_u16(wav + 20u, 1u);
    test_put_u16(wav + 22u, 1u);
    test_put_u32(wav + 24u, 44100u);
    test_put_u32(wav + 28u, 88200u);
    test_put_u16(wav + 32u, 2u);
    test_put_u16(wav + 34u, 16u);
    std::memcpy(wav + 36u, "data", 4u);
    test_put_u32(wav + 40u, sizeof(pcm));
    std::memcpy(wav + 44u, pcm, sizeof(pcm));
    const bool wav_written = platform_file_write(wav_file_path, wav, sizeof(wav));
    CHECK(wav_written);
    if (!tga_written || !wav_written) {
        if (tga_written) CHECK(std::remove(tga_file_path) == 0);
        if (wav_written) CHECK(std::remove(wav_file_path) == 0);
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    RegistryFixture fixture;
    const bool initialized = init_fixture(&fixture, 4u, owned.path);
    CHECK(initialized);
    if (!initialized) {
        CHECK(std::remove(tga_file_path) == 0);
        CHECK(std::remove(wav_file_path) == 0);
        CHECK(test_release_owned_directory(&owned));
        return;
    }
    AssetHandle texture = asset_load_texture_tga(&fixture.registry, tga_asset_path,
                                                  ASSET_LIFETIME_LEVEL);
    AssetHandle sound = asset_load_sound_wav(&fixture.registry, wav_asset_path,
                                              ASSET_LIFETIME_LEVEL);
    CHECK(!handle_is_null(texture.h) && !handle_is_null(sound.h));
    AssetTextureView texture_view{};
    AssetSoundView sound_view{};
    CHECK(asset_get_texture(&fixture.registry, texture, &texture_view));
    CHECK(texture_view.width == 1u && texture_view.height == 1u);
    CHECK(texture_view.rgba8[0] == 0x33u && texture_view.rgba8[1] == 0x22u &&
          texture_view.rgba8[2] == 0x11u && texture_view.rgba8[3] == 0x44u);
    CHECK(asset_get_sound(&fixture.registry, sound, &sound_view));
    CHECK(sound_view.sample_rate == 44100u && sound_view.frame_count == 2u &&
          std::memcmp(sound_view.pcm, pcm, sizeof(pcm)) == 0);
    CHECK(fixture.io.offset == fixture.registry.io_base_offset);

    const size_t level_offset = fixture.level.offset;
    const uint32_t live_count = fixture.registry.live_count;
    AssetHandle missing = asset_load_texture_tga(&fixture.registry,
                                                  "missing_asset.tga",
                                                  ASSET_LIFETIME_LEVEL);
    CHECK(handle_is_null(missing.h));
    CHECK(fixture.level.offset == level_offset &&
          fixture.registry.live_count == live_count &&
          fixture.io.offset == fixture.registry.io_base_offset);

    asset_registry_shutdown(&fixture.registry, 0u);
    CHECK(std::remove(tga_file_path) == 0);
    CHECK(std::remove(wav_file_path) == 0);
    CHECK(test_release_owned_directory(&owned));
}

TEST(assets, baked_mba_loads_through_the_single_type_tag_path) {
    OwnedTestDirectory owned{};
    owned.scope_custody = INVALID_HANDLE_VALUE;
    owned.directory_custody = INVALID_HANDLE_VALUE;
    const bool directory_owned = test_create_owned_directory("mba", &owned);
    CHECK(directory_owned);
    if (!directory_owned) return;

    char valid_path[256];
    char bad_magic_path[256];
    char bad_version_path[256];
    const int valid_chars = std::snprintf(valid_path, sizeof(valid_path), "%s/fixture.mba",
                                          owned.path);
    const int magic_chars = std::snprintf(bad_magic_path, sizeof(bad_magic_path),
                                          "%s/bad_magic.mba", owned.path);
    const int version_chars = std::snprintf(bad_version_path, sizeof(bad_version_path),
                                            "%s/bad_version.mba", owned.path);
    const bool paths_ready = valid_chars > 0 && (size_t)valid_chars < sizeof(valid_path) &&
        magic_chars > 0 && (size_t)magic_chars < sizeof(bad_magic_path) &&
        version_chars > 0 && (size_t)version_chars < sizeof(bad_version_path);
    CHECK(paths_ready);
    if (!paths_ready) {
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    const uint8_t pixels[] = {0x30u, 0x20u, 0x10u, 0xFFu};
    const MbaTextureSource source{pixels, sizeof(pixels), 1u, 1u};
    uint8_t baked[64]{};
    size_t baked_bytes = 0u;
    CHECK(mba_encode_texture(baked, sizeof(baked), asset_id("fixture.mba"), &source,
                             &baked_bytes));
    uint8_t bad_magic[64];
    uint8_t bad_version[64];
    std::memcpy(bad_magic, baked, baked_bytes);
    std::memcpy(bad_version, baked, baked_bytes);
    bad_magic[0] ^= 0x01u;
    bad_version[4] = (uint8_t)(MBA_VERSION + 1u);

    const bool valid_written = platform_file_write(valid_path, baked, baked_bytes);
    const bool magic_written = platform_file_write(bad_magic_path, bad_magic, baked_bytes);
    const bool version_written = platform_file_write(bad_version_path, bad_version, baked_bytes);
    CHECK(valid_written && magic_written && version_written);
    if (!valid_written || !magic_written || !version_written) {
        if (valid_written) CHECK(std::remove(valid_path) == 0);
        if (magic_written) CHECK(std::remove(bad_magic_path) == 0);
        if (version_written) CHECK(std::remove(bad_version_path) == 0);
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    RegistryFixture fixture;
    const bool initialized = init_fixture(&fixture, 2u, owned.path);
    CHECK(initialized);
    if (initialized) {
        const size_t level_before = fixture.level.offset;
        CHECK(handle_is_null(asset_load(&fixture.registry, "bad_magic.mba",
                                        ASSET_LIFETIME_LEVEL).h));
        CHECK(handle_is_null(asset_load(&fixture.registry, "bad_version.mba",
                                        ASSET_LIFETIME_LEVEL).h));
        CHECK(fixture.registry.live_count == 0u && fixture.fake.creates == 0u &&
              fixture.level.offset == level_before &&
              fixture.io.offset == fixture.registry.io_base_offset);

        const AssetHandle texture = asset_load(&fixture.registry, "fixture.mba",
                                               ASSET_LIFETIME_LEVEL);
        CHECK(!handle_is_null(texture.h));
        AssetTextureView view{};
        CHECK(asset_get_texture(&fixture.registry, texture, &view));
        CHECK(view.id == asset_id("fixture.mba") && view.width == 1u && view.height == 1u &&
              std::memcmp(view.rgba8, pixels, sizeof(pixels)) == 0);
        CHECK(fixture.fake.creates == 1u);
        asset_registry_shutdown(&fixture.registry, 0u);
    }

    CHECK(std::remove(valid_path) == 0);
    CHECK(std::remove(bad_magic_path) == 0);
    CHECK(std::remove(bad_version_path) == 0);
    CHECK(test_release_owned_directory(&owned));
}
