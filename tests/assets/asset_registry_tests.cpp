#include "test.h"

#include "assets/assets.h"
#include "assets/mba.h"
#include "assets/tga.h"
#include "assets/wav.h"
#include "platform/platform.h"
#include "win32_test_directory.h"

#include <cstdio>
#include <cstring>

struct FakeTextures {
    uint32_t creates;
    uint32_t destroys;
    uint32_t last_frames;
    Handle next;
    bool fail_create;
};

static TextureHandle fake_create_texture(void* user, const uint8_t* pixels,
                                         uint32_t width, uint32_t height) {
    FakeTextures* fake = (FakeTextures*)user;
    TextureHandle result{HANDLE_NULL};
    if (!fake || !pixels || width == 0u || height == 0u) return result;
    if (fake->fail_create) return result;
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
                          const char* asset_root = ".",
                          AssetCatalog catalog = AssetCatalog{}) {
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
    config.catalog = catalog;
    return asset_registry_init(&fixture->registry, &fixture->persistent, &fixture->level,
                               &fixture->global, &fixture->io, renderer, config);
}

static void sort_catalog_entries(AssetCatalogEntry* entries, uint32_t count) {
    for (uint32_t i = 1u; i < count; ++i) {
        const AssetCatalogEntry value = entries[i];
        uint32_t position = i;
        while (position != 0u && entries[position - 1u].id > value.id) {
            entries[position] = entries[position - 1u];
            --position;
        }
        entries[position] = value;
    }
}

TEST(assets, catalog_validation_is_transactional) {
    alignas(16) uint8_t persistent_storage[65536];
    alignas(16) uint8_t level_storage[256];
    alignas(16) uint8_t global_storage[256];
    alignas(16) uint8_t io_storage[256];
    Arena persistent, level, global, io;
    AssetRegistry registry;
    AssetRendererApi renderer{};

    AssetCatalogEntry valid[2] = {
        {asset_id("a.tga"), ASSET_TYPE_TEXTURE, "a.tga", "a.tga.mba"},
        {asset_id("b.wav"), ASSET_TYPE_SOUND, "b.wav", "b.wav.mba"},
    };
    sort_catalog_entries(valid, 2u);
    AssetCatalogEntry unsorted[2] = {valid[1], valid[0]};
    AssetCatalogEntry wrong_id = valid[0];
    wrong_id.id ^= 1u;
    if (wrong_id.id == ASSET_ID_NULL) wrong_id.id = 1u;
    AssetCatalogEntry wrong_type = valid[0];
    wrong_type.type = ASSET_TYPE_NONE;
    AssetCatalogEntry wrong_baked = valid[0];
    wrong_baked.baked_path = "other.tga.mba";
    AssetCatalogEntry noncanonical = valid[0];
    noncanonical.logical_path = "A.TGA";
    const AssetCatalog invalid[] = {
        {nullptr, 1u},
        {unsorted, 2u},
        {&wrong_id, 1u},
        {&wrong_type, 1u},
        {&wrong_baked, 1u},
        {&noncanonical, 1u},
    };

    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        arena_init_fixed(&persistent, persistent_storage, sizeof(persistent_storage));
        arena_init_fixed(&level, level_storage, sizeof(level_storage));
        arena_init_fixed(&global, global_storage, sizeof(global_storage));
        arena_init_fixed(&io, io_storage, sizeof(io_storage));
        std::memset(&registry, 0xA5, sizeof(registry));
        const AssetRegistry before = registry;
        AssetRegistryConfig config = asset_registry_config_default(".");
        config.capacity = 2u;
        config.catalog = invalid[i];
        CHECK(!asset_registry_init(&registry, &persistent, &level, &global, &io,
                                   renderer, config));
        CHECK(std::memcmp(&registry, &before, sizeof(registry)) == 0);
        CHECK(persistent.offset == 0u && level.offset == 0u &&
              global.offset == 0u && io.offset == 0u);
    }

    arena_init_fixed(&persistent, persistent_storage, sizeof(persistent_storage));
    arena_init_fixed(&level, level_storage, sizeof(level_storage));
    arena_init_fixed(&global, global_storage, sizeof(global_storage));
    arena_init_fixed(&io, io_storage, sizeof(io_storage));
    AssetCatalogEntry* overlapping = reinterpret_cast<AssetCatalogEntry*>(
        persistent_storage + 1024u);
    *overlapping = valid[0];
    std::memset(&registry, 0xA5, sizeof(registry));
    const AssetRegistry before = registry;
    AssetRegistryConfig config = asset_registry_config_default(".");
    config.capacity = 2u;
    config.catalog = AssetCatalog{overlapping, 1u};
    CHECK(!asset_registry_init(&registry, &persistent, &level, &global, &io,
                               renderer, config));
    CHECK(std::memcmp(&registry, &before, sizeof(registry)) == 0);
    CHECK(persistent.offset == 0u && level.offset == 0u &&
          global.offset == 0u && io.offset == 0u);
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

TEST(assets, baked_tga_and_wav_match_direct_parser_values) {
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
        tga_file_path, sizeof(tga_file_path), "%s/%s.mba", owned.path, tga_asset_path);
    const int wav_chars = std::snprintf(
        wav_file_path, sizeof(wav_file_path), "%s/%s.mba", owned.path, wav_asset_path);
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
    alignas(16) uint8_t direct_storage[256];
    Arena direct_arena;
    arena_init_fixed(&direct_arena, direct_storage, sizeof(direct_storage));
    TgaImage direct_texture{};
    WavPcm direct_sound{};
    CHECK(tga_decode(tga, sizeof(tga), arena_allocator(&direct_arena),
                     &direct_texture));
    CHECK(wav_decode_pcm(wav, sizeof(wav), arena_allocator(&direct_arena),
                         &direct_sound));

    const AssetId tga_id = asset_id("fixture.tga");
    const AssetId wav_id = asset_id("fixture.wav");
    uint8_t baked_tga[128] = {};
    uint8_t baked_wav[128] = {};
    size_t baked_tga_bytes = 0u;
    size_t baked_wav_bytes = 0u;
    MbaTextureSource texture_source{direct_texture.rgba8,
                                    direct_texture.width * direct_texture.height * 4u,
                                    direct_texture.width, direct_texture.height};
    MbaSoundSource sound_source{direct_sound.pcm, direct_sound.pcm_bytes,
                                direct_sound.sample_rate, direct_sound.channels,
                                direct_sound.bits_per_sample};
    CHECK(mba_encode_texture(baked_tga, sizeof(baked_tga), tga_id,
                             &texture_source, &baked_tga_bytes));
    CHECK(mba_encode_sound(baked_wav, sizeof(baked_wav), wav_id,
                           &sound_source, &baked_wav_bytes));
    const bool tga_written = platform_file_write(
        tga_file_path, baked_tga, baked_tga_bytes);
    const bool wav_written = platform_file_write(
        wav_file_path, baked_wav, baked_wav_bytes);
    CHECK(tga_written && wav_written);
    if (!tga_written || !wav_written) {
        if (tga_written) CHECK(std::remove(tga_file_path) == 0);
        if (wav_written) CHECK(std::remove(wav_file_path) == 0);
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    AssetCatalogEntry entries[2] = {
        {tga_id, ASSET_TYPE_TEXTURE, "fixture.tga", "fixture.tga.mba"},
        {wav_id, ASSET_TYPE_SOUND, "fixture.wav", "fixture.wav.mba"},
    };
    sort_catalog_entries(entries, 2u);
    const AssetCatalog catalog{entries, 2u};
    RegistryFixture fixture;
    const bool initialized = init_fixture(&fixture, 4u, owned.path, catalog);
    CHECK(initialized);
    if (!initialized) {
        CHECK(std::remove(tga_file_path) == 0);
        CHECK(std::remove(wav_file_path) == 0);
        CHECK(test_release_owned_directory(&owned));
        return;
    }
    AssetHandle texture = asset_load(&fixture.registry, tga_id,
                                     ASSET_LIFETIME_LEVEL);
    AssetHandle sound = asset_load(&fixture.registry, wav_id,
                                   ASSET_LIFETIME_GLOBAL);
    CHECK(!handle_is_null(texture.h) && !handle_is_null(sound.h));
    AssetTextureView texture_view{};
    AssetSoundView sound_view{};
    CHECK(asset_get_texture(&fixture.registry, texture, &texture_view));
    CHECK(texture_view.width == direct_texture.width &&
          texture_view.height == direct_texture.height &&
          texture_view.rgba8_bytes == texture_source.rgba8_bytes);
    CHECK(std::memcmp(texture_view.rgba8, direct_texture.rgba8,
                      texture_view.rgba8_bytes) == 0);
    CHECK(asset_get_sound(&fixture.registry, sound, &sound_view));
    CHECK(sound_view.sample_rate == direct_sound.sample_rate &&
          sound_view.channels == direct_sound.channels &&
          sound_view.bits_per_sample == direct_sound.bits_per_sample &&
          sound_view.pcm_bytes == direct_sound.pcm_bytes &&
          std::memcmp(sound_view.pcm, direct_sound.pcm,
                      direct_sound.pcm_bytes) == 0);
    CHECK(fixture.io.offset == fixture.registry.io_base_offset);
    AssetHandle sound_again = asset_load(&fixture.registry, wav_id,
                                         ASSET_LIFETIME_GLOBAL);
    CHECK(sound_again.h == sound.h);
    CHECK(asset_global_refcount(&fixture.registry, sound) == 2u);

    const size_t level_offset = fixture.level.offset;
    const uint32_t live_count = fixture.registry.live_count;
    AssetHandle missing = asset_load(&fixture.registry, asset_id("missing.tga"),
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

TEST(assets, baked_runtime_failures_are_mutation_free) {
    OwnedTestDirectory owned{};
    owned.scope_custody = INVALID_HANDLE_VALUE;
    owned.directory_custody = INVALID_HANDLE_VALUE;
    const bool directory_owned = test_create_owned_directory("asset-failures", &owned);
    CHECK(directory_owned);
    if (!directory_owned) return;

    static const uint8_t rgba[] = {9u, 8u, 7u, 6u};
    static const uint8_t pcm[] = {1u, 0u};
    const char* logical_paths[] = {
        "bad-id.tga", "bad-type.tga", "budget.tga", "corrupt.tga",
        "renderer-fail.tga",
    };
    AssetCatalogEntry entries[5];
    for (uint32_t i = 0u; i < 5u; ++i) {
        static const char* baked_paths[] = {
            "bad-id.tga.mba", "bad-type.tga.mba", "budget.tga.mba",
            "corrupt.tga.mba", "renderer-fail.tga.mba",
        };
        entries[i] = AssetCatalogEntry{
            asset_id_normalized_bytes(logical_paths[i], std::strlen(logical_paths[i])),
            ASSET_TYPE_TEXTURE, logical_paths[i], baked_paths[i]};
    }
    sort_catalog_entries(entries, 5u);
    const AssetCatalog catalog{entries, 5u};

    uint8_t texture_bytes[128] = {};
    uint8_t renderer_bytes[128] = {};
    uint8_t wrong_id_bytes[128] = {};
    uint8_t sound_bytes[128] = {};
    size_t texture_size = 0u;
    size_t renderer_size = 0u;
    size_t wrong_id_size = 0u;
    size_t sound_size = 0u;
    MbaTextureSource texture_source{rgba, sizeof(rgba), 1u, 1u};
    MbaSoundSource sound_source{pcm, sizeof(pcm), 48000u, 1u, 16u};
    const AssetId budget_id = asset_id("budget.tga");
    CHECK(mba_encode_texture(texture_bytes, sizeof(texture_bytes), budget_id,
                             &texture_source, &texture_size));
    CHECK(mba_encode_texture(renderer_bytes, sizeof(renderer_bytes),
                             asset_id("renderer-fail.tga"), &texture_source,
                             &renderer_size));
    CHECK(mba_encode_texture(wrong_id_bytes, sizeof(wrong_id_bytes),
                             asset_id("other.tga"), &texture_source,
                             &wrong_id_size));
    CHECK(mba_encode_sound(sound_bytes, sizeof(sound_bytes),
                           asset_id("bad-type.tga"), &sound_source,
                           &sound_size));

    struct FileFixture { const char* name; const void* bytes; size_t size; } files[] = {
        {"bad-id.tga.mba", wrong_id_bytes, wrong_id_size},
        {"bad-type.tga.mba", sound_bytes, sound_size},
        {"budget.tga.mba", texture_bytes, texture_size},
        {"corrupt.tga.mba", texture_bytes, 31u},
        {"renderer-fail.tga.mba", renderer_bytes, renderer_size},
        {"stale.tga.mba", texture_bytes, texture_size},
    };
    char file_paths[6][256] = {};
    bool files_ready = true;
    for (uint32_t i = 0u; i < 6u; ++i) {
        const int chars = std::snprintf(file_paths[i], sizeof(file_paths[i]),
                                        "%s/%s", owned.path, files[i].name);
        files_ready = files_ready && chars > 0 &&
            (size_t)chars < sizeof(file_paths[i]) &&
            platform_file_write(file_paths[i], files[i].bytes, files[i].size);
    }
    CHECK(files_ready);
    if (!files_ready) {
        for (uint32_t i = 0u; i < 6u; ++i) std::remove(file_paths[i]);
        CHECK(test_release_owned_directory(&owned));
        return;
    }

    RegistryFixture fixture;
    CHECK(init_fixture(&fixture, 8u, owned.path, catalog));
    const size_t level_base = fixture.level.offset;
    const size_t global_base = fixture.global.offset;
    const uint32_t live_base = fixture.registry.live_count;
    const uint32_t map_base = fixture.registry.by_id.count;

    const AssetId rejected[] = {
        asset_id("bad-id.tga"), asset_id("bad-type.tga"),
        asset_id("corrupt.tga"), asset_id("stale.tga"),
    };
    for (AssetId id : rejected) {
        CHECK(handle_is_null(asset_load(&fixture.registry, id,
                                        ASSET_LIFETIME_LEVEL).h));
        CHECK(fixture.level.offset == level_base && fixture.global.offset == global_base);
        CHECK(fixture.io.offset == fixture.registry.io_base_offset);
        CHECK(fixture.registry.live_count == live_base &&
              fixture.registry.by_id.count == map_base);
        CHECK(fixture.fake.creates == 0u && fixture.fake.destroys == 0u);
    }

    fixture.level.offset = fixture.level.reserved - 1u;
    const size_t short_offset = fixture.level.offset;
    CHECK(handle_is_null(asset_load(&fixture.registry, budget_id,
                                    ASSET_LIFETIME_LEVEL).h));
    CHECK(fixture.level.offset == short_offset && fixture.fake.creates == 0u);
    fixture.level.offset = level_base;

    fixture.fake.fail_create = true;
    CHECK(handle_is_null(asset_load(&fixture.registry,
                                    asset_id("renderer-fail.tga"),
                                    ASSET_LIFETIME_LEVEL).h));
    CHECK(fixture.level.offset == level_base && fixture.global.offset == global_base);
    CHECK(fixture.io.offset == fixture.registry.io_base_offset);
    CHECK(fixture.registry.live_count == live_base &&
          fixture.registry.by_id.count == map_base);
    CHECK(fixture.fake.creates == 0u && fixture.fake.destroys == 0u);

    asset_registry_shutdown(&fixture.registry, 0u);
    for (uint32_t i = 0u; i < 6u; ++i) CHECK(std::remove(file_paths[i]) == 0);
    CHECK(test_release_owned_directory(&owned));
}
