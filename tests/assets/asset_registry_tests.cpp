#include "test.h"

#include "assets/assets.h"

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

static bool init_fixture(RegistryFixture* fixture, uint32_t capacity) {
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
    AssetRegistryConfig config = asset_registry_config_default(".");
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
