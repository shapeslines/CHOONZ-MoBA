#include "assets/assets.h"

#include "core/assert.h"

#include <string.h>

static AssetHandle null_asset_handle(void) {
    AssetHandle result{HANDLE_NULL};
    return result;
}

static bool add_required_region(size_t* total, size_t count, size_t item_size,
                                size_t alignment) {
    if (!total || item_size == 0u || alignment == 0u || count > SIZE_MAX / item_size)
        return false;
    const size_t bytes = count * item_size;
    const size_t slack = alignment - 1u;
    if (*total > SIZE_MAX - slack || *total + slack > SIZE_MAX - bytes) return false;
    *total += slack + bytes;
    return true;
}

static uint32_t asset_map_capacity(uint32_t capacity) {
    if (capacity == 0u || capacity > (HANDLE_INDEX_MASK + 1u)) return 0u;
    const uint64_t wanted = (uint64_t)capacity * 2u;
    uint32_t result = 16u;
    while ((uint64_t)result < wanted) {
        if (result > UINT32_MAX / 2u) return 0u;
        result *= 2u;
    }
    return result;
}

AssetRegistryConfig asset_registry_config_default(const char* asset_root) {
    AssetRegistryConfig config{};
    config.capacity = 256u;
    config.max_file_bytes = 24u * 1024u * 1024u;
    config.asset_root = asset_root;
    return config;
}

size_t asset_registry_memory_required(AssetRegistryConfig config) {
    const uint32_t map_capacity = asset_map_capacity(config.capacity);
    if (map_capacity == 0u || !config.asset_root || config.max_file_bytes == 0u) return 0u;

    size_t total = 0u;
#define ASSET_ADD_ARRAY(T) \
    if (!add_required_region(&total, config.capacity, sizeof(T), alignof(T))) return 0u
    ASSET_ADD_ARRAY(uint16_t);
    ASSET_ADD_ARRAY(uint8_t);
    ASSET_ADD_ARRAY(uint8_t);
    ASSET_ADD_ARRAY(uint8_t);
    ASSET_ADD_ARRAY(uint8_t);
    ASSET_ADD_ARRAY(AssetId);
    ASSET_ADD_ARRAY(void*);
    ASSET_ADD_ARRAY(size_t);
    ASSET_ADD_ARRAY(Handle);
    ASSET_ADD_ARRAY(uint32_t);
    ASSET_ADD_ARRAY(uint32_t);
    ASSET_ADD_ARRAY(uint32_t);
    ASSET_ADD_ARRAY(uint32_t);
    ASSET_ADD_ARRAY(char*);
    ASSET_ADD_ARRAY(uint16_t);
    ASSET_ADD_ARRAY(uint32_t);
#undef ASSET_ADD_ARRAY
    if (!add_required_region(&total, map_capacity,
                             sizeof(HashMap<AssetId, uint32_t>::Slot),
                             alignof(HashMap<AssetId, uint32_t>::Slot))) return 0u;
    return total;
}

static bool valid_arena(const Arena* arena) {
    return arena && arena->base && arena->offset <= arena->committed &&
           arena->committed <= arena->reserved;
}

static bool copy_asset_root(const char* root, char* out, uint16_t* out_length) {
    if (!root || !out || !out_length) return false;
    size_t length = strlen(root);
    if (length == 0u || length >= ASSET_PATH_MAX) return false;
    while (length > 1u && (root[length - 1u] == '/' || root[length - 1u] == '\\')) {
        if (length == 3u && root[1] == ':') break;
        --length;
    }
    if (length == 0u || length > UINT16_MAX) return false;
    memcpy(out, root, length);
    out[length] = '\0';
    *out_length = (uint16_t)length;
    return true;
}

bool asset_registry_init(AssetRegistry* registry, Arena* persistent_arena,
                         Arena* level_arena, Arena* global_arena, Arena* io_arena,
                         AssetRendererApi renderer, AssetRegistryConfig config) {
    if (!registry || !valid_arena(persistent_arena) || !valid_arena(level_arena) ||
        !valid_arena(global_arena) || !valid_arena(io_arena) ||
        persistent_arena == level_arena || persistent_arena == global_arena ||
        persistent_arena == io_arena || level_arena == global_arena ||
        level_arena == io_arena || global_arena == io_arena) return false;

    const size_t required = asset_registry_memory_required(config);
    const uint32_t map_capacity = asset_map_capacity(config.capacity);
    if (required == 0u || required > persistent_arena->reserved - persistent_arena->offset)
        return false;

    AssetRegistry next{};
    if (!copy_asset_root(config.asset_root, next.asset_root, &next.asset_root_length))
        return false;

#define ASSET_PUSH_ARRAY(field, T) \
    next.field = ARENA_PUSH_ARRAY(persistent_arena, T, config.capacity)
    ASSET_PUSH_ARRAY(generations, uint16_t);
    ASSET_PUSH_ARRAY(live, uint8_t);
    ASSET_PUSH_ARRAY(types, uint8_t);
    ASSET_PUSH_ARRAY(states, uint8_t);
    ASSET_PUSH_ARRAY(lifetimes, uint8_t);
    ASSET_PUSH_ARRAY(ids, AssetId);
    ASSET_PUSH_ARRAY(cpu_blobs, void*);
    ASSET_PUSH_ARRAY(cpu_blob_bytes, size_t);
    ASSET_PUSH_ARRAY(gpu_handles, Handle);
    ASSET_PUSH_ARRAY(refcounts, uint32_t);
    ASSET_PUSH_ARRAY(meta0, uint32_t);
    ASSET_PUSH_ARRAY(meta1, uint32_t);
    ASSET_PUSH_ARRAY(meta2, uint32_t);
    ASSET_PUSH_ARRAY(paths, char*);
    ASSET_PUSH_ARRAY(path_lengths, uint16_t);
    ASSET_PUSH_ARRAY(free_stack, uint32_t);
#undef ASSET_PUSH_ARRAY

    typedef HashMap<AssetId, uint32_t>::Slot MapSlot;
    next.by_id.slots = ARENA_PUSH_ARRAY(persistent_arena, MapSlot, map_capacity);
    next.by_id.cap = map_capacity;
    next.by_id.mask = map_capacity - 1u;
    next.by_id.count = 0u;
    next.by_id.alloc = arena_allocator(persistent_arena);

    next.capacity = config.capacity;
    next.persistent_arena = persistent_arena;
    next.level_arena = level_arena;
    next.global_arena = global_arena;
    next.io_arena = io_arena;
    next.level_base_offset = level_arena->offset;
    next.global_base_offset = global_arena->offset;
    next.io_base_offset = io_arena->offset;
    next.max_file_bytes = config.max_file_bytes;
    next.renderer = renderer;
    next.initialized = 1u;
    *registry = next;
    return true;
}

static const uint32_t* asset_map_get(const AssetRegistry* registry, AssetId id) {
    if (!registry || registry->by_id.cap == 0u) return nullptr;
    const uint64_t hash = hashmap_hash(id);
    const uint32_t mask = registry->by_id.mask;
    uint32_t pos = (uint32_t)hash & mask;
    uint32_t dib = 0u;
    for (;;) {
        const HashMap<AssetId, uint32_t>::Slot* slot = &registry->by_id.slots[pos];
        if (!slot->occupied) return nullptr;
        if (slot->hash == hash && slot->key == id) return &slot->val;
        const uint32_t existing_dib = (pos - ((uint32_t)slot->hash & mask)) & mask;
        if (existing_dib < dib) return nullptr;
        pos = (pos + 1u) & mask;
        ++dib;
    }
}

static bool handle_slot(const AssetRegistry* registry, AssetHandle handle,
                        uint32_t* out_slot) {
    if (!registry || !registry->initialized || handle_is_null(handle.h)) return false;
    const uint32_t slot = handle_index(handle.h);
    const uint32_t generation = handle_gen(handle.h);
    if (generation == 0u || slot >= registry->capacity || !registry->live[slot] ||
        registry->generations[slot] != generation) return false;
    if (out_slot) *out_slot = slot;
    return true;
}

bool asset_registry_valid(const AssetRegistry* registry, AssetHandle handle) {
    return handle_slot(registry, handle, nullptr);
}

AssetHandle asset_registry_find(const AssetRegistry* registry, AssetId id) {
    if (!registry || !registry->initialized || id == ASSET_ID_NULL) return null_asset_handle();
    const uint32_t* found = asset_map_get(registry, id);
    if (!found || *found >= registry->capacity || !registry->live[*found])
        return null_asset_handle();
    AssetHandle result{handle_make(*found, registry->generations[*found])};
    return result;
}

AssetHandle asset_registry_find_path(const AssetRegistry* registry, const char* path) {
    char normalized[ASSET_PATH_MAX];
    size_t length = 0u;
    if (!asset_path_normalize(path, normalized, sizeof(normalized), &length))
        return null_asset_handle();
    const AssetId id = asset_id_normalized_bytes(normalized, length);
    AssetHandle handle = asset_registry_find(registry, id);
    uint32_t slot = 0u;
    if (!handle_slot(registry, handle, &slot) || registry->path_lengths[slot] != length ||
        memcmp(registry->paths[slot], normalized, length) != 0) return null_asset_handle();
    return handle;
}

static bool arena_has_room(const Arena* arena, size_t data_bytes, size_t path_bytes) {
    if (!valid_arena(arena)) return false;
    const size_t data_slack = MEM_DEFAULT_ALIGN - 1u;
    if (data_bytes > SIZE_MAX - data_slack) return false;
    size_t required = data_slack + data_bytes;
    if (required > SIZE_MAX - path_bytes) return false;
    required += path_bytes;
    return required <= arena->reserved - arena->offset;
}

static bool next_slot_available(const AssetRegistry* registry, uint32_t* out_slot) {
    if (!registry || !out_slot || registry->live_count >= registry->capacity) return false;
    uint32_t slot = 0u;
    if (registry->free_count != 0u) {
        slot = registry->free_stack[registry->free_count - 1u];
        if (slot >= registry->capacity || registry->live[slot] ||
            registry->generations[slot] == 0u ||
            registry->generations[slot] > HANDLE_GEN_MASK) return false;
    } else {
        slot = registry->next_fresh;
        if (slot >= registry->capacity || registry->live[slot] ||
            registry->generations[slot] != 0u) return false;
    }
    *out_slot = slot;
    return true;
}

static AssetHandle existing_asset(AssetRegistry* registry, AssetId id,
                                  const char* path, size_t path_length,
                                  AssetType type, AssetLifetime lifetime,
                                  bool* out_found) {
    *out_found = false;
    const uint32_t* found = asset_map_get(registry, id);
    if (!found) return null_asset_handle();
    *out_found = true;
    const uint32_t slot = *found;
    if (slot >= registry->capacity || !registry->live[slot] ||
        registry->ids[slot] != id || registry->types[slot] != (uint8_t)type ||
        registry->lifetimes[slot] != (uint8_t)lifetime ||
        registry->states[slot] != ASSET_STATE_READY ||
        registry->path_lengths[slot] != path_length ||
        memcmp(registry->paths[slot], path, path_length) != 0) {
        return null_asset_handle();
    }
    if (lifetime == ASSET_LIFETIME_GLOBAL) {
        if (registry->refcounts[slot] == UINT32_MAX) return null_asset_handle();
        ++registry->refcounts[slot];
    }
    AssetHandle result{handle_make(slot, registry->generations[slot])};
    return result;
}

static AssetHandle commit_asset(AssetRegistry* registry, const char* path,
                                size_t path_length, AssetId id, AssetType type,
                                AssetLifetime lifetime, const void* source,
                                size_t source_bytes, Handle gpu_handle,
                                uint32_t meta0, uint32_t meta1, uint32_t meta2) {
    Arena* arena = lifetime == ASSET_LIFETIME_LEVEL
        ? registry->level_arena : registry->global_arena;
    if (!arena_has_room(arena, source_bytes, path_length + 1u)) return null_asset_handle();

    uint32_t slot = 0u;
    if (!next_slot_available(registry, &slot)) return null_asset_handle();
    const size_t saved_offset = arena->offset;
    void* cpu = arena_push(arena, source_bytes, MEM_DEFAULT_ALIGN);
    char* stored_path = (char*)arena_push(arena, path_length + 1u, alignof(char));
    if (!cpu || !stored_path) {
        arena->offset = saved_offset;
        return null_asset_handle();
    }
    memmove(cpu, source, source_bytes);
    memcpy(stored_path, path, path_length + 1u);

    if (registry->free_count != 0u) --registry->free_count;
    else ++registry->next_fresh;
    if (registry->generations[slot] == 0u) registry->generations[slot] = 1u;
    registry->live[slot] = 1u;
    registry->types[slot] = (uint8_t)type;
    registry->states[slot] = ASSET_STATE_READY;
    registry->lifetimes[slot] = (uint8_t)lifetime;
    registry->ids[slot] = id;
    registry->cpu_blobs[slot] = cpu;
    registry->cpu_blob_bytes[slot] = source_bytes;
    registry->gpu_handles[slot] = gpu_handle;
    registry->refcounts[slot] = lifetime == ASSET_LIFETIME_GLOBAL ? 1u : 0u;
    registry->meta0[slot] = meta0;
    registry->meta1[slot] = meta1;
    registry->meta2[slot] = meta2;
    registry->paths[slot] = stored_path;
    registry->path_lengths[slot] = (uint16_t)path_length;
    ++registry->live_count;
    hashmap_set(&registry->by_id, id, slot);

    AssetHandle result{handle_make(slot, registry->generations[slot])};
    return result;
}

AssetHandle asset_register_texture(AssetRegistry* registry, const char* path,
                                   AssetLifetime lifetime, AssetTextureSource source) {
    if (!registry || !registry->initialized || !source.rgba8 || source.width == 0u ||
        source.height == 0u || lifetime > ASSET_LIFETIME_GLOBAL ||
        !registry->renderer.create_texture || !registry->renderer.destroy_texture)
        return null_asset_handle();
    const uint64_t pixels = (uint64_t)source.width * source.height;
    if (pixels > SIZE_MAX / 4u) return null_asset_handle();
    const size_t bytes = (size_t)pixels * 4u;

    char normalized[ASSET_PATH_MAX];
    size_t path_length = 0u;
    if (!asset_path_normalize(path, normalized, sizeof(normalized), &path_length))
        return null_asset_handle();
    const AssetId id = asset_id_normalized_bytes(normalized, path_length);
    bool found = false;
    AssetHandle existing = existing_asset(registry, id, normalized, path_length,
                                          ASSET_TYPE_TEXTURE, lifetime, &found);
    if (found) return existing;

    uint32_t slot = 0u;
    Arena* arena = lifetime == ASSET_LIFETIME_LEVEL
        ? registry->level_arena : registry->global_arena;
    if (!next_slot_available(registry, &slot) ||
        !arena_has_room(arena, bytes, path_length + 1u)) return null_asset_handle();

    const TextureHandle gpu = registry->renderer.create_texture(
        registry->renderer.user, source.rgba8, source.width, source.height);
    if (handle_is_null(gpu.h)) return null_asset_handle();
    AssetHandle result = commit_asset(registry, normalized, path_length, id,
                                      ASSET_TYPE_TEXTURE, lifetime, source.rgba8,
                                      bytes, gpu.h, source.width, source.height, 0u);
    if (handle_is_null(result.h))
        registry->renderer.destroy_texture(registry->renderer.user, gpu, 0u);
    return result;
}

AssetHandle asset_register_sound(AssetRegistry* registry, const char* path,
                                 AssetLifetime lifetime, AssetSoundSource source) {
    if (!registry || !registry->initialized || !source.pcm || source.pcm_bytes == 0u ||
        source.sample_rate == 0u || source.channels == 0u || source.channels > 2u ||
        (source.bits_per_sample != 8u && source.bits_per_sample != 16u &&
         source.bits_per_sample != 24u && source.bits_per_sample != 32u) ||
        lifetime > ASSET_LIFETIME_GLOBAL) return null_asset_handle();
    const uint32_t bytes_per_frame = (uint32_t)source.channels *
                                     ((uint32_t)source.bits_per_sample / 8u);
    if (bytes_per_frame == 0u || source.pcm_bytes % bytes_per_frame != 0u ||
        source.pcm_bytes / bytes_per_frame > UINT32_MAX) return null_asset_handle();

    char normalized[ASSET_PATH_MAX];
    size_t path_length = 0u;
    if (!asset_path_normalize(path, normalized, sizeof(normalized), &path_length))
        return null_asset_handle();
    const AssetId id = asset_id_normalized_bytes(normalized, path_length);
    bool found = false;
    AssetHandle existing = existing_asset(registry, id, normalized, path_length,
                                          ASSET_TYPE_SOUND, lifetime, &found);
    if (found) return existing;
    return commit_asset(registry, normalized, path_length, id, ASSET_TYPE_SOUND,
                        lifetime, source.pcm, source.pcm_bytes, HANDLE_NULL,
                        source.sample_rate,
                        ((uint32_t)source.channels << 16u) | source.bits_per_sample,
                        (uint32_t)(source.pcm_bytes / bytes_per_frame));
}

bool asset_get_texture(const AssetRegistry* registry, AssetHandle handle,
                       AssetTextureView* out) {
    uint32_t slot = 0u;
    if (!out || !handle_slot(registry, handle, &slot) ||
        registry->types[slot] != ASSET_TYPE_TEXTURE ||
        registry->states[slot] != ASSET_STATE_READY) return false;
    AssetTextureView next{};
    next.id = registry->ids[slot];
    next.rgba8 = (const uint8_t*)registry->cpu_blobs[slot];
    next.rgba8_bytes = registry->cpu_blob_bytes[slot];
    next.width = registry->meta0[slot];
    next.height = registry->meta1[slot];
    next.gpu.h = registry->gpu_handles[slot];
    *out = next;
    return true;
}

bool asset_get_sound(const AssetRegistry* registry, AssetHandle handle,
                     AssetSoundView* out) {
    uint32_t slot = 0u;
    if (!out || !handle_slot(registry, handle, &slot) ||
        registry->types[slot] != ASSET_TYPE_SOUND ||
        registry->states[slot] != ASSET_STATE_READY) return false;
    AssetSoundView next{};
    next.id = registry->ids[slot];
    next.pcm = (const uint8_t*)registry->cpu_blobs[slot];
    next.pcm_bytes = registry->cpu_blob_bytes[slot];
    next.sample_rate = registry->meta0[slot];
    next.channels = (uint16_t)(registry->meta1[slot] >> 16u);
    next.bits_per_sample = (uint16_t)(registry->meta1[slot] & 0xffffu);
    next.frame_count = registry->meta2[slot];
    *out = next;
    return true;
}

uint32_t asset_global_refcount(const AssetRegistry* registry, AssetHandle handle) {
    uint32_t slot = 0u;
    if (!handle_slot(registry, handle, &slot) ||
        registry->lifetimes[slot] != ASSET_LIFETIME_GLOBAL) return 0u;
    return registry->refcounts[slot];
}

static void retire_slot(AssetRegistry* registry, uint32_t slot,
                        uint32_t frames_until_free) {
    if (!registry->live[slot]) return;
    if (registry->types[slot] == ASSET_TYPE_TEXTURE &&
        !handle_is_null(registry->gpu_handles[slot]) &&
        registry->renderer.destroy_texture) {
        TextureHandle texture{registry->gpu_handles[slot]};
        registry->renderer.destroy_texture(registry->renderer.user, texture,
                                           frames_until_free);
    }
    (void)hashmap_remove(&registry->by_id, registry->ids[slot]);
    uint32_t generation = (uint32_t)registry->generations[slot] + 1u;
    ASSERT(generation <= HANDLE_GEN_MASK);
    if (generation > HANDLE_GEN_MASK) generation = 1u;
    registry->generations[slot] = (uint16_t)generation;
    registry->live[slot] = 0u;
    registry->types[slot] = ASSET_TYPE_NONE;
    registry->states[slot] = ASSET_STATE_EMPTY;
    registry->lifetimes[slot] = ASSET_LIFETIME_LEVEL;
    registry->ids[slot] = ASSET_ID_NULL;
    registry->cpu_blobs[slot] = nullptr;
    registry->cpu_blob_bytes[slot] = 0u;
    registry->gpu_handles[slot] = HANDLE_NULL;
    registry->refcounts[slot] = 0u;
    registry->meta0[slot] = registry->meta1[slot] = registry->meta2[slot] = 0u;
    registry->paths[slot] = nullptr;
    registry->path_lengths[slot] = 0u;
    registry->free_stack[registry->free_count++] = slot;
    --registry->live_count;
}

bool asset_release_global(AssetRegistry* registry, AssetHandle handle,
                          uint32_t frames_until_free) {
    uint32_t slot = 0u;
    if (!handle_slot(registry, handle, &slot) ||
        registry->lifetimes[slot] != ASSET_LIFETIME_GLOBAL ||
        registry->refcounts[slot] == 0u) return false;
    --registry->refcounts[slot];
    if (registry->refcounts[slot] == 0u) retire_slot(registry, slot, frames_until_free);
    return true;
}

void assets_unload_level(AssetRegistry* registry, uint32_t frames_until_free) {
    if (!registry || !registry->initialized) return;
    for (uint32_t slot = 0u; slot < registry->next_fresh; ++slot) {
        if (registry->live[slot] && registry->lifetimes[slot] == ASSET_LIFETIME_LEVEL)
            retire_slot(registry, slot, frames_until_free);
    }
    registry->level_arena->offset = registry->level_base_offset;
}

void asset_registry_shutdown(AssetRegistry* registry, uint32_t frames_until_free) {
    if (!registry || !registry->initialized) return;
    for (uint32_t slot = 0u; slot < registry->next_fresh; ++slot)
        if (registry->live[slot]) retire_slot(registry, slot, frames_until_free);
    registry->level_arena->offset = registry->level_base_offset;
    registry->global_arena->offset = registry->global_base_offset;
    registry->io_arena->offset = registry->io_base_offset;
    registry->initialized = 0u;
}
