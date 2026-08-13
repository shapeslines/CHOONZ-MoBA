#include "sim/components.h"

static bool valid_pool_shape(uint32_t entity_capacity, uint32_t capacity) {
    return entity_capacity > 0u && entity_capacity <= HANDLE_INDEX_MASK + 1u &&
           capacity > 0u && capacity <= entity_capacity;
}

static bool add_required(size_t* total, size_t bytes, size_t alignment) {
    if (!total || alignment == 0u || bytes > SIZE_MAX - (alignment - 1u) ||
        *total > SIZE_MAX - (bytes + alignment - 1u)) return false;
    *total += bytes + alignment - 1u;
    return true;
}

static bool arena_has_budget(const Arena* arena, size_t required) {
    return arena && arena->base && arena->offset <= arena->reserved &&
           required <= arena->reserved - arena->offset;
}

static size_t typed_pool_memory_required(uint32_t entity_capacity, uint32_t capacity,
                                         uint32_t array_count, size_t element_size,
                                         size_t element_alignment) {
    if (!valid_pool_shape(entity_capacity, capacity)) return 0u;
    size_t total = component_pool_memory_required(entity_capacity, capacity);
    size_t bytes = element_size * static_cast<size_t>(capacity);
    for (uint32_t i = 0; i < array_count; ++i) {
        if (!add_required(&total, bytes, element_alignment)) return 0u;
    }
    return total;
}

size_t transform_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    return typed_pool_memory_required(entity_capacity, capacity, 3u,
                                      sizeof(mm::fix), alignof(mm::fix));
}

bool transform_pool_init(TransformPool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity) {
    size_t required = transform_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    TransformPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t bytes = sizeof(mm::fix) * static_cast<size_t>(capacity);
    staged.position_x = static_cast<mm::fix*>(arena_push_zero(arena, bytes, alignof(mm::fix)));
    staged.position_y = static_cast<mm::fix*>(arena_push_zero(arena, bytes, alignof(mm::fix)));
    staged.facing = static_cast<mm::fix*>(arena_push_zero(arena, bytes, alignof(mm::fix)));
    *pool = staged;
    return true;
}

bool transform_pool_add(TransformPool* pool, EntityId entity,
                        mm::fix position_x, mm::fix position_y, mm::fix facing) {
    if (!pool || !pool->position_x || !pool->position_y || !pool->facing) return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    pool->position_x[dense] = position_x;
    pool->position_y[dense] = position_y;
    pool->facing[dense] = facing;
    return true;
}

bool transform_pool_remove(TransformPool* pool, EntityId entity) {
    if (!pool || !pool->position_x || !pool->position_y || !pool->facing) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    if (result.removed_dense != result.moved_from_dense) {
        pool->position_x[result.removed_dense] = pool->position_x[result.moved_from_dense];
        pool->position_y[result.removed_dense] = pool->position_y[result.moved_from_dense];
        pool->facing[result.removed_dense] = pool->facing[result.moved_from_dense];
    }
    pool->position_x[result.moved_from_dense] = 0;
    pool->position_y[result.moved_from_dense] = 0;
    pool->facing[result.moved_from_dense] = 0;
    return true;
}

bool transform_pool_has(const TransformPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool transform_pool_get(TransformPool* pool, EntityId entity, TransformView* view) {
    if (!pool || !view) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE || !pool->position_x || !pool->position_y ||
        !pool->facing) return false;
    TransformView staged{&pool->position_x[dense], &pool->position_y[dense], &pool->facing[dense]};
    *view = staged;
    return true;
}

bool transform_pool_get_const(const TransformPool* pool, EntityId entity,
                              ConstTransformView* view) {
    if (!pool || !view) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE || !pool->position_x || !pool->position_y ||
        !pool->facing) return false;
    ConstTransformView staged{
        &pool->position_x[dense], &pool->position_y[dense], &pool->facing[dense]};
    *view = staged;
    return true;
}

size_t velocity_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    return typed_pool_memory_required(entity_capacity, capacity, 2u,
                                      sizeof(mm::fix), alignof(mm::fix));
}

bool velocity_pool_init(VelocityPool* pool, Arena* arena,
                        uint32_t entity_capacity, uint32_t capacity) {
    size_t required = velocity_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    VelocityPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t bytes = sizeof(mm::fix) * static_cast<size_t>(capacity);
    staged.velocity_x = static_cast<mm::fix*>(arena_push_zero(arena, bytes, alignof(mm::fix)));
    staged.velocity_y = static_cast<mm::fix*>(arena_push_zero(arena, bytes, alignof(mm::fix)));
    *pool = staged;
    return true;
}

bool velocity_pool_add(VelocityPool* pool, EntityId entity,
                       mm::fix velocity_x, mm::fix velocity_y) {
    if (!pool || !pool->velocity_x || !pool->velocity_y) return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    pool->velocity_x[dense] = velocity_x;
    pool->velocity_y[dense] = velocity_y;
    return true;
}

bool velocity_pool_remove(VelocityPool* pool, EntityId entity) {
    if (!pool || !pool->velocity_x || !pool->velocity_y) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    if (result.removed_dense != result.moved_from_dense) {
        pool->velocity_x[result.removed_dense] = pool->velocity_x[result.moved_from_dense];
        pool->velocity_y[result.removed_dense] = pool->velocity_y[result.moved_from_dense];
    }
    pool->velocity_x[result.moved_from_dense] = 0;
    pool->velocity_y[result.moved_from_dense] = 0;
    return true;
}

bool velocity_pool_has(const VelocityPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool velocity_pool_get(VelocityPool* pool, EntityId entity, VelocityView* view) {
    if (!pool || !view) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE || !pool->velocity_x || !pool->velocity_y) return false;
    VelocityView staged{&pool->velocity_x[dense], &pool->velocity_y[dense]};
    *view = staged;
    return true;
}

size_t health_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    return typed_pool_memory_required(entity_capacity, capacity, 3u,
                                      sizeof(uint32_t), alignof(uint32_t));
}

bool health_pool_init(HealthPool* pool, Arena* arena,
                      uint32_t entity_capacity, uint32_t capacity) {
    size_t required = health_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    HealthPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t signed_bytes = sizeof(int32_t) * static_cast<size_t>(capacity);
    size_t unsigned_bytes = sizeof(uint32_t) * static_cast<size_t>(capacity);
    staged.current = static_cast<int32_t*>(arena_push_zero(arena, signed_bytes, alignof(int32_t)));
    staged.maximum = static_cast<int32_t*>(arena_push_zero(arena, signed_bytes, alignof(int32_t)));
    staged.damage_cooldown = static_cast<uint32_t*>(
        arena_push_zero(arena, unsigned_bytes, alignof(uint32_t)));
    *pool = staged;
    return true;
}

bool health_pool_add(HealthPool* pool, EntityId entity,
                     int32_t current, int32_t maximum, uint32_t damage_cooldown) {
    if (!pool || !pool->current || !pool->maximum || !pool->damage_cooldown ||
        current < 0 || maximum < 0 || current > maximum) return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    pool->current[dense] = current;
    pool->maximum[dense] = maximum;
    pool->damage_cooldown[dense] = damage_cooldown;
    return true;
}

bool health_pool_remove(HealthPool* pool, EntityId entity) {
    if (!pool || !pool->current || !pool->maximum || !pool->damage_cooldown) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    if (result.removed_dense != result.moved_from_dense) {
        pool->current[result.removed_dense] = pool->current[result.moved_from_dense];
        pool->maximum[result.removed_dense] = pool->maximum[result.moved_from_dense];
        pool->damage_cooldown[result.removed_dense] = pool->damage_cooldown[result.moved_from_dense];
    }
    pool->current[result.moved_from_dense] = 0;
    pool->maximum[result.moved_from_dense] = 0;
    pool->damage_cooldown[result.moved_from_dense] = 0u;
    return true;
}

bool health_pool_has(const HealthPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool health_pool_get(HealthPool* pool, EntityId entity, HealthView* view) {
    if (!pool || !view) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE || !pool->current || !pool->maximum ||
        !pool->damage_cooldown) return false;
    HealthView staged{&pool->current[dense], &pool->maximum[dense],
                      &pool->damage_cooldown[dense]};
    *view = staged;
    return true;
}
