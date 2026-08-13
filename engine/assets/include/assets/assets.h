#pragma once

#include <stddef.h>
#include <stdint.h>

#include "assets/asset_id.h"
#include "core/handle.h"
#include "core/hashmap.h"
#include "core/mem.h"

typedef enum AssetType {
    ASSET_TYPE_NONE = 0,
    ASSET_TYPE_TEXTURE,
    ASSET_TYPE_SOUND,
} AssetType;

typedef enum AssetState {
    ASSET_STATE_EMPTY = 0,
    ASSET_STATE_LOADING,
    ASSET_STATE_READY,
    ASSET_STATE_FAILED,
} AssetState;

typedef enum AssetLifetime {
    ASSET_LIFETIME_LEVEL = 0,
    ASSET_LIFETIME_GLOBAL,
} AssetLifetime;

typedef struct AssetRegistryConfig {
    uint32_t    capacity;
    size_t      max_file_bytes;
    const char* asset_root;
} AssetRegistryConfig;

typedef TextureHandle (*AssetCreateTextureFn)(void* user, const uint8_t* rgba8,
                                               uint32_t width, uint32_t height);
typedef void (*AssetDestroyTextureFn)(void* user, TextureHandle texture,
                                      uint32_t frames_until_free);

// Renderer ownership stays explicit without making eng_assets depend on Vulkan or
// a concrete renderer backend. The sandbox binds these to the M2.5 renderer seam;
// headless tests bind a deterministic fake.
typedef struct AssetRendererApi {
    void*                 user;
    AssetCreateTextureFn  create_texture;
    AssetDestroyTextureFn destroy_texture;
} AssetRendererApi;

typedef struct AssetTextureSource {
    const uint8_t* rgba8;
    uint32_t       width;
    uint32_t       height;
} AssetTextureSource;

typedef struct AssetSoundSource {
    const uint8_t* pcm;
    size_t         pcm_bytes;
    uint32_t       sample_rate;
    uint16_t       channels;
    uint16_t       bits_per_sample;
} AssetSoundSource;

typedef struct AssetTextureView {
    AssetId        id;
    const uint8_t* rgba8;
    size_t         rgba8_bytes;
    uint32_t       width;
    uint32_t       height;
    TextureHandle  gpu;
} AssetTextureView;

typedef struct AssetSoundView {
    AssetId        id;
    const uint8_t* pcm;
    size_t         pcm_bytes;
    uint32_t       sample_rate;
    uint16_t       channels;
    uint16_t       bits_per_sample;
    uint32_t       frame_count;
} AssetSoundView;

// Fixed-capacity SoA registry. Paths and CPU payloads live in the selected lifetime
// arena; all identity/index state lives in the persistent arena. `state` reserves
// ASSET_STATE_LOADING for the later async path even though M4.0 loads synchronously.
typedef struct AssetRegistry {
    uint16_t* generations;
    uint8_t*  live;
    uint8_t*  types;
    uint8_t*  states;
    uint8_t*  lifetimes;
    AssetId*  ids;
    void**    cpu_blobs;
    size_t*   cpu_blob_bytes;
    Handle*   gpu_handles;
    uint32_t* refcounts;
    uint32_t* meta0;
    uint32_t* meta1;
    uint32_t* meta2;
    char**    paths;
    uint16_t* path_lengths;
    uint32_t* free_stack;

    HashMap<AssetId, uint32_t> by_id;
    uint32_t capacity;
    uint32_t live_count;
    uint32_t next_fresh;
    uint32_t free_count;

    Arena* persistent_arena;
    Arena* level_arena;
    Arena* global_arena;
    Arena* io_arena;
    size_t level_base_offset;
    size_t global_base_offset;
    size_t io_base_offset;
    size_t max_file_bytes;
    AssetRendererApi renderer;
    char asset_root[ASSET_PATH_MAX];
    uint16_t asset_root_length;
    uint8_t initialized;
} AssetRegistry;

AssetRegistryConfig asset_registry_config_default(const char* asset_root);
size_t asset_registry_memory_required(AssetRegistryConfig config);

// Transactional: invalid configuration or an under-budget persistent arena leaves
// both `registry` and every arena offset untouched.
bool asset_registry_init(AssetRegistry* registry, Arena* persistent_arena,
                         Arena* level_arena, Arena* global_arena, Arena* io_arena,
                         AssetRendererApi renderer, AssetRegistryConfig config);

AssetHandle asset_register_texture(AssetRegistry* registry, const char* path,
                                   AssetLifetime lifetime, AssetTextureSource source);
AssetHandle asset_register_sound(AssetRegistry* registry, const char* path,
                                 AssetLifetime lifetime, AssetSoundSource source);

AssetHandle asset_registry_find(const AssetRegistry* registry, AssetId id);
AssetHandle asset_registry_find_path(const AssetRegistry* registry, const char* path);
bool asset_registry_valid(const AssetRegistry* registry, AssetHandle handle);
bool asset_get_texture(const AssetRegistry* registry, AssetHandle handle,
                       AssetTextureView* out);
bool asset_get_sound(const AssetRegistry* registry, AssetHandle handle,
                     AssetSoundView* out);
uint32_t asset_global_refcount(const AssetRegistry* registry, AssetHandle handle);

// Global references release individually. Level assets release as one deterministic
// batch and the level arena rewinds in O(1); both paths invalidate stale handles.
bool asset_release_global(AssetRegistry* registry, AssetHandle handle,
                          uint32_t frames_until_free);
void assets_unload_level(AssetRegistry* registry, uint32_t frames_until_free);
void asset_registry_shutdown(AssetRegistry* registry, uint32_t frames_until_free);
