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

// ------------------------------------------------------------------ M5.2 hero combat

static const uint32_t HERO_ACTION_COOLDOWN_STRIDE = SIM_MAX_ACTION_SLOTS;

size_t hero_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    if (!valid_pool_shape(entity_capacity, capacity)) return 0u;
    if (static_cast<size_t>(capacity) > SIZE_MAX / (sizeof(uint32_t) * HERO_ACTION_COOLDOWN_STRIDE))
        return 0u;
    size_t total = component_pool_memory_required(entity_capacity, capacity);
    if (total == 0u) return 0u;
    size_t count = static_cast<size_t>(capacity);
    if (!add_required(&total, sizeof(uint16_t) * count, alignof(uint16_t)) ||
        !add_required(&total, sizeof(int32_t) * count, alignof(int32_t)) ||
        !add_required(&total, sizeof(uint32_t) * count, alignof(uint32_t)) ||
        !add_required(&total, sizeof(uint32_t) * count * HERO_ACTION_COOLDOWN_STRIDE,
                      alignof(uint32_t)) ||
        !add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(EntityId) * count, alignof(EntityId)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix))) return 0u;
    return total;
}

bool hero_pool_init(HeroPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity) {
    size_t required = hero_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    HeroPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t count = static_cast<size_t>(capacity);
    staged.def_index = static_cast<uint16_t*>(
        arena_push_zero(arena, sizeof(uint16_t) * count, alignof(uint16_t)));
    staged.resource = static_cast<int32_t*>(
        arena_push_zero(arena, sizeof(int32_t) * count, alignof(int32_t)));
    staged.basic_attack_cooldown = static_cast<uint32_t*>(
        arena_push_zero(arena, sizeof(uint32_t) * count, alignof(uint32_t)));
    staged.action_cooldown = static_cast<uint32_t*>(arena_push_zero(
        arena, sizeof(uint32_t) * count * HERO_ACTION_COOLDOWN_STRIDE, alignof(uint32_t)));
    staged.pending_kind = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.pending_slot = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.pending_target = static_cast<EntityId*>(
        arena_push_zero(arena, sizeof(EntityId) * count, alignof(EntityId)));
    staged.pending_point_x = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    staged.pending_point_y = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    *pool = staged;
    return true;
}

static bool hero_pool_storage_ok(const HeroPool* pool) {
    return pool && pool->def_index && pool->resource && pool->basic_attack_cooldown &&
           pool->action_cooldown && pool->pending_kind && pool->pending_slot &&
           pool->pending_target && pool->pending_point_x && pool->pending_point_y;
}

static void hero_pool_clear_row(HeroPool* pool, uint32_t dense) {
    pool->def_index[dense] = 0u;
    pool->resource[dense] = 0;
    pool->basic_attack_cooldown[dense] = 0u;
    for (uint32_t slot = 0u; slot < HERO_ACTION_COOLDOWN_STRIDE; ++slot)
        pool->action_cooldown[dense * HERO_ACTION_COOLDOWN_STRIDE + slot] = 0u;
    pool->pending_kind[dense] = 0u;
    pool->pending_slot[dense] = 0u;
    pool->pending_target[dense] = EntityId{HANDLE_NULL};
    pool->pending_point_x[dense] = 0;
    pool->pending_point_y[dense] = 0;
}

bool hero_pool_add(HeroPool* pool, EntityId entity, uint16_t def_index, int32_t resource) {
    if (!hero_pool_storage_ok(pool) || resource < 0) return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    hero_pool_clear_row(pool, dense);
    pool->def_index[dense] = def_index;
    pool->resource[dense] = resource;
    return true;
}

bool hero_pool_remove(HeroPool* pool, EntityId entity) {
    if (!hero_pool_storage_ok(pool)) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    uint32_t removed = result.removed_dense;
    uint32_t moved = result.moved_from_dense;
    if (removed != moved) {
        pool->def_index[removed] = pool->def_index[moved];
        pool->resource[removed] = pool->resource[moved];
        pool->basic_attack_cooldown[removed] = pool->basic_attack_cooldown[moved];
        for (uint32_t slot = 0u; slot < HERO_ACTION_COOLDOWN_STRIDE; ++slot) {
            pool->action_cooldown[removed * HERO_ACTION_COOLDOWN_STRIDE + slot] =
                pool->action_cooldown[moved * HERO_ACTION_COOLDOWN_STRIDE + slot];
        }
        pool->pending_kind[removed] = pool->pending_kind[moved];
        pool->pending_slot[removed] = pool->pending_slot[moved];
        pool->pending_target[removed] = pool->pending_target[moved];
        pool->pending_point_x[removed] = pool->pending_point_x[moved];
        pool->pending_point_y[removed] = pool->pending_point_y[moved];
    }
    hero_pool_clear_row(pool, moved);
    return true;
}

bool hero_pool_has(const HeroPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool hero_pool_get(HeroPool* pool, EntityId entity, HeroView* view) {
    if (!pool || !view || !hero_pool_storage_ok(pool)) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    HeroView staged{&pool->def_index[dense],
                    &pool->resource[dense],
                    &pool->basic_attack_cooldown[dense],
                    &pool->action_cooldown[dense * HERO_ACTION_COOLDOWN_STRIDE],
                    &pool->pending_kind[dense],
                    &pool->pending_slot[dense],
                    &pool->pending_target[dense],
                    &pool->pending_point_x[dense],
                    &pool->pending_point_y[dense]};
    *view = staged;
    return true;
}

bool hero_pool_def_index(const HeroPool* pool, EntityId entity, uint16_t* out_def_index) {
    if (!pool || !pool->def_index || !out_def_index) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    *out_def_index = pool->def_index[dense];
    return true;
}

size_t projectile_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    if (!valid_pool_shape(entity_capacity, capacity)) return 0u;
    size_t total = component_pool_memory_required(entity_capacity, capacity);
    if (total == 0u) return 0u;
    size_t count = static_cast<size_t>(capacity);
    if (!add_required(&total, sizeof(EntityId) * count, alignof(EntityId)) ||
        !add_required(&total, sizeof(EntityId) * count, alignof(EntityId)) ||
        !add_required(&total, sizeof(uint16_t) * count, alignof(uint16_t)) ||
        !add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix)) ||
        !add_required(&total, sizeof(uint16_t) * count, alignof(uint16_t))) return 0u;
    return total;
}

bool projectile_pool_init(ProjectilePool* pool, Arena* arena,
                          uint32_t entity_capacity, uint32_t capacity) {
    size_t required = projectile_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    ProjectilePool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t count = static_cast<size_t>(capacity);
    staged.source = static_cast<EntityId*>(
        arena_push_zero(arena, sizeof(EntityId) * count, alignof(EntityId)));
    staged.target = static_cast<EntityId*>(
        arena_push_zero(arena, sizeof(EntityId) * count, alignof(EntityId)));
    staged.def_index = static_cast<uint16_t*>(
        arena_push_zero(arena, sizeof(uint16_t) * count, alignof(uint16_t)));
    staged.action_slot = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.effect_index = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.position_x = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    staged.position_y = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    staged.speed = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    staged.remaining_ticks = static_cast<uint16_t*>(
        arena_push_zero(arena, sizeof(uint16_t) * count, alignof(uint16_t)));
    *pool = staged;
    return true;
}

static bool projectile_pool_storage_ok(const ProjectilePool* pool) {
    return pool && pool->source && pool->target && pool->def_index && pool->action_slot &&
           pool->effect_index && pool->position_x && pool->position_y && pool->speed &&
           pool->remaining_ticks;
}

static void projectile_pool_clear_row(ProjectilePool* pool, uint32_t dense) {
    pool->source[dense] = EntityId{HANDLE_NULL};
    pool->target[dense] = EntityId{HANDLE_NULL};
    pool->def_index[dense] = 0u;
    pool->action_slot[dense] = 0u;
    pool->effect_index[dense] = 0u;
    pool->position_x[dense] = 0;
    pool->position_y[dense] = 0;
    pool->speed[dense] = 0;
    pool->remaining_ticks[dense] = 0u;
}

bool projectile_pool_add(ProjectilePool* pool, EntityId entity, EntityId source,
                         EntityId target, uint16_t def_index, uint8_t action_slot,
                         uint8_t effect_index, mm::fix position_x, mm::fix position_y,
                         mm::fix speed, uint16_t remaining_ticks) {
    if (!projectile_pool_storage_ok(pool) || speed <= 0 || remaining_ticks == 0u ||
        action_slot >= SIM_MAX_ACTION_SLOTS || effect_index >= SIM_MAX_EFFECTS_PER_ACTION)
        return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    pool->source[dense] = source;
    pool->target[dense] = target;
    pool->def_index[dense] = def_index;
    pool->action_slot[dense] = action_slot;
    pool->effect_index[dense] = effect_index;
    pool->position_x[dense] = position_x;
    pool->position_y[dense] = position_y;
    pool->speed[dense] = speed;
    pool->remaining_ticks[dense] = remaining_ticks;
    return true;
}

bool projectile_pool_remove(ProjectilePool* pool, EntityId entity) {
    if (!projectile_pool_storage_ok(pool)) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    uint32_t removed = result.removed_dense;
    uint32_t moved = result.moved_from_dense;
    if (removed != moved) {
        pool->source[removed] = pool->source[moved];
        pool->target[removed] = pool->target[moved];
        pool->def_index[removed] = pool->def_index[moved];
        pool->action_slot[removed] = pool->action_slot[moved];
        pool->effect_index[removed] = pool->effect_index[moved];
        pool->position_x[removed] = pool->position_x[moved];
        pool->position_y[removed] = pool->position_y[moved];
        pool->speed[removed] = pool->speed[moved];
        pool->remaining_ticks[removed] = pool->remaining_ticks[moved];
    }
    projectile_pool_clear_row(pool, moved);
    return true;
}

bool projectile_pool_has(const ProjectilePool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool projectile_pool_get(ProjectilePool* pool, EntityId entity, ProjectileView* view) {
    if (!pool || !view || !projectile_pool_storage_ok(pool)) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    ProjectileView staged{&pool->source[dense],       &pool->target[dense],
                          &pool->def_index[dense],    &pool->action_slot[dense],
                          &pool->effect_index[dense], &pool->position_x[dense],
                          &pool->position_y[dense],   &pool->speed[dense],
                          &pool->remaining_ticks[dense]};
    *view = staged;
    return true;
}

size_t status_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    if (!valid_pool_shape(entity_capacity, capacity)) return 0u;
    size_t total = component_pool_memory_required(entity_capacity, capacity);
    if (total == 0u) return 0u;
    size_t count = static_cast<size_t>(capacity);
    if (!add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(uint8_t) * count, alignof(uint8_t)) ||
        !add_required(&total, sizeof(uint16_t) * count, alignof(uint16_t)) ||
        !add_required(&total, sizeof(int32_t) * count, alignof(int32_t)) ||
        !add_required(&total, sizeof(mm::fix) * count, alignof(mm::fix))) return 0u;
    return total;
}

bool status_pool_init(StatusPool* pool, Arena* arena,
                      uint32_t entity_capacity, uint32_t capacity) {
    size_t required = status_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    StatusPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    size_t count = static_cast<size_t>(capacity);
    staged.effect_type = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.stack_count = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * count, alignof(uint8_t)));
    staged.remaining_ticks = static_cast<uint16_t*>(
        arena_push_zero(arena, sizeof(uint16_t) * count, alignof(uint16_t)));
    staged.magnitude = static_cast<int32_t*>(
        arena_push_zero(arena, sizeof(int32_t) * count, alignof(int32_t)));
    staged.scalar = static_cast<mm::fix*>(
        arena_push_zero(arena, sizeof(mm::fix) * count, alignof(mm::fix)));
    *pool = staged;
    return true;
}

static bool status_pool_storage_ok(const StatusPool* pool) {
    return pool && pool->effect_type && pool->stack_count && pool->remaining_ticks &&
           pool->magnitude && pool->scalar;
}

static void status_pool_clear_row(StatusPool* pool, uint32_t dense) {
    pool->effect_type[dense] = 0u;
    pool->stack_count[dense] = 0u;
    pool->remaining_ticks[dense] = 0u;
    pool->magnitude[dense] = 0;
    pool->scalar[dense] = 0;
}

// Refresh-and-max per (target, effect_type); a different effect type replaces the
// row. Additive stacking would be a separate canonical-state break.
bool status_pool_add(StatusPool* pool, EntityId entity, uint8_t effect_type,
                     uint16_t remaining_ticks, int32_t magnitude, mm::fix scalar) {
    if (!status_pool_storage_ok(pool) || effect_type == SIM_EFFECT_NONE ||
        remaining_ticks == 0u || magnitude < 0 || scalar < 0) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) {
        if (!component_pool_add(&pool->membership, entity, &dense)) return false;
        status_pool_clear_row(pool, dense);
        pool->effect_type[dense] = effect_type;
        pool->remaining_ticks[dense] = remaining_ticks;
        pool->magnitude[dense] = magnitude;
        pool->scalar[dense] = scalar;
        pool->stack_count[dense] = 1u;
        return true;
    }
    if (pool->effect_type[dense] != effect_type) {
        pool->effect_type[dense] = effect_type;
        pool->remaining_ticks[dense] = remaining_ticks;
        pool->magnitude[dense] = magnitude;
        pool->scalar[dense] = scalar;
        pool->stack_count[dense] = 1u;
        return true;
    }
    if (remaining_ticks > pool->remaining_ticks[dense])
        pool->remaining_ticks[dense] = remaining_ticks;
    if (magnitude > pool->magnitude[dense]) {
        pool->magnitude[dense] = magnitude;
        pool->scalar[dense] = scalar;
    }
    if (pool->stack_count[dense] < 255u) ++pool->stack_count[dense];
    return true;
}

bool status_pool_remove(StatusPool* pool, EntityId entity) {
    if (!status_pool_storage_ok(pool)) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    uint32_t removed = result.removed_dense;
    uint32_t moved = result.moved_from_dense;
    if (removed != moved) {
        pool->effect_type[removed] = pool->effect_type[moved];
        pool->stack_count[removed] = pool->stack_count[moved];
        pool->remaining_ticks[removed] = pool->remaining_ticks[moved];
        pool->magnitude[removed] = pool->magnitude[moved];
        pool->scalar[removed] = pool->scalar[moved];
    }
    status_pool_clear_row(pool, moved);
    return true;
}

bool status_pool_has(const StatusPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool status_pool_get(StatusPool* pool, EntityId entity, StatusView* view) {
    if (!pool || !view || !status_pool_storage_ok(pool)) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    StatusView staged{&pool->effect_type[dense], &pool->stack_count[dense],
                      &pool->remaining_ticks[dense], &pool->magnitude[dense],
                      &pool->scalar[dense]};
    *view = staged;
    return true;
}
