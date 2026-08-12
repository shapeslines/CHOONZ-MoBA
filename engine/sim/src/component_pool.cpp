#include "sim/component_pool.h"

static bool layout_advance(const Arena* arena, size_t* offset, size_t size, size_t alignment) {
    if (!arena || !arena->base || !offset || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u || *offset > arena->reserved) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(arena->base);
    if (*offset > static_cast<size_t>(UINTPTR_MAX - base)) return false;
    uintptr_t current = base + *offset;
    size_t padding = static_cast<size_t>(
        (alignment - (current & static_cast<uintptr_t>(alignment - 1u))) & (alignment - 1u));
    if (padding > SIZE_MAX - *offset) return false;
    size_t start = *offset + padding;
    if (size > SIZE_MAX - start) return false;
    size_t end = start + size;
    if (end > arena->reserved) return false;
    *offset = end;
    return true;
}

static bool entity_fits(const ComponentPool* pool, EntityId entity) {
    return pool && handle_gen(entity.h) != 0u && handle_index(entity.h) < pool->entity_capacity;
}

size_t component_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    if (entity_capacity == 0u || entity_capacity > HANDLE_INDEX_MASK + 1u ||
        capacity == 0u || capacity > entity_capacity) return 0u;

    size_t sparse_bytes = sizeof(uint32_t) * static_cast<size_t>(entity_capacity);
    size_t dense_bytes = sizeof(EntityId) * static_cast<size_t>(capacity);
    return sparse_bytes + (alignof(uint32_t) - 1u) +
           dense_bytes + (alignof(EntityId) - 1u);
}

bool component_pool_init(ComponentPool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity) {
    if (!pool || !arena || component_pool_memory_required(entity_capacity, capacity) == 0u ||
        !arena->base || arena->offset > arena->reserved) return false;

    size_t end = arena->offset;
    if (!layout_advance(arena, &end,
                        sizeof(uint32_t) * static_cast<size_t>(entity_capacity),
                        alignof(uint32_t)) ||
        !layout_advance(arena, &end, sizeof(EntityId) * static_cast<size_t>(capacity),
                        alignof(EntityId))) return false;

    ComponentPool staged{};
    staged.sparse = static_cast<uint32_t*>(arena_push_zero(
        arena, sizeof(uint32_t) * static_cast<size_t>(entity_capacity), alignof(uint32_t)));
    staged.dense_entities = static_cast<EntityId*>(arena_push_zero(
        arena, sizeof(EntityId) * static_cast<size_t>(capacity), alignof(EntityId)));
    staged.entity_capacity = entity_capacity;
    staged.capacity = capacity;
    *pool = staged;
    return true;
}

bool component_pool_has(const ComponentPool* pool, EntityId entity) {
    if (!entity_fits(pool, entity) || !pool->sparse || !pool->dense_entities) return false;
    uint32_t encoded_dense = pool->sparse[handle_index(entity.h)];
    if (encoded_dense == 0u) return false;
    uint32_t dense = encoded_dense - 1u;
    return dense < pool->count && dense < pool->capacity &&
           pool->dense_entities[dense].h == entity.h;
}

uint32_t component_pool_dense_index(const ComponentPool* pool, EntityId entity) {
    if (!component_pool_has(pool, entity)) return COMPONENT_POOL_INVALID_DENSE;
    return pool->sparse[handle_index(entity.h)] - 1u;
}

bool component_pool_add(ComponentPool* pool, EntityId entity, uint32_t* dense_index) {
    if (!entity_fits(pool, entity) || !pool->sparse || !pool->dense_entities ||
        pool->count >= pool->capacity) return false;

    uint32_t entity_index = handle_index(entity.h);
    if (pool->sparse[entity_index] != 0u) return false;

    uint32_t dense = pool->count;
    pool->dense_entities[dense] = entity;
    pool->sparse[entity_index] = dense + 1u;
    ++pool->count;
    if (dense_index) *dense_index = dense;
    return true;
}

bool component_pool_remove(ComponentPool* pool, EntityId entity,
                           ComponentPoolRemoveResult* result) {
    if (!component_pool_has(pool, entity) || pool->count == 0u) return false;

    uint32_t entity_index = handle_index(entity.h);
    uint32_t removed_dense = pool->sparse[entity_index] - 1u;
    uint32_t last_dense = pool->count - 1u;
    EntityId moved{HANDLE_NULL};
    if (removed_dense != last_dense) {
        moved = pool->dense_entities[last_dense];
        uint32_t moved_index = handle_index(moved.h);
        if (handle_gen(moved.h) == 0u || moved_index >= pool->entity_capacity ||
            pool->sparse[moved_index] != last_dense + 1u) return false;
    }

    ComponentPoolRemoveResult staged{};
    staged.removed_dense = removed_dense;
    staged.moved_from_dense = last_dense;
    staged.moved_entity = moved;

    if (removed_dense != last_dense) {
        pool->dense_entities[removed_dense] = moved;
        pool->sparse[handle_index(moved.h)] = removed_dense + 1u;
    }
    pool->dense_entities[last_dense] = EntityId{HANDLE_NULL};
    pool->sparse[entity_index] = 0u;
    --pool->count;
    if (result) *result = staged;
    return true;
}
