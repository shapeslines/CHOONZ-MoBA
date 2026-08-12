#pragma once

#include <stddef.h>
#include <stdint.h>

#include "math/fix.h"
#include "sim/component_pool.h"

typedef struct TransformView {
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* facing;
} TransformView;

typedef struct TransformPool {
    ComponentPool membership;
    mm::fix* position_x;
    mm::fix* position_y;
    mm::fix* facing;
} TransformPool;

size_t transform_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool transform_pool_init(TransformPool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity);
bool transform_pool_add(TransformPool* pool, EntityId entity,
                        mm::fix position_x, mm::fix position_y, mm::fix facing);
bool transform_pool_remove(TransformPool* pool, EntityId entity);
bool transform_pool_has(const TransformPool* pool, EntityId entity);
bool transform_pool_get(TransformPool* pool, EntityId entity, TransformView* view);

typedef struct VelocityView {
    mm::fix* velocity_x;
    mm::fix* velocity_y;
} VelocityView;

typedef struct VelocityPool {
    ComponentPool membership;
    mm::fix* velocity_x;
    mm::fix* velocity_y;
} VelocityPool;

size_t velocity_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool velocity_pool_init(VelocityPool* pool, Arena* arena,
                        uint32_t entity_capacity, uint32_t capacity);
bool velocity_pool_add(VelocityPool* pool, EntityId entity,
                       mm::fix velocity_x, mm::fix velocity_y);
bool velocity_pool_remove(VelocityPool* pool, EntityId entity);
bool velocity_pool_has(const VelocityPool* pool, EntityId entity);
bool velocity_pool_get(VelocityPool* pool, EntityId entity, VelocityView* view);

typedef struct HealthView {
    int32_t* current;
    int32_t* maximum;
    uint32_t* damage_cooldown;
} HealthView;

typedef struct HealthPool {
    ComponentPool membership;
    int32_t* current;
    int32_t* maximum;
    uint32_t* damage_cooldown;
} HealthPool;

size_t health_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool health_pool_init(HealthPool* pool, Arena* arena,
                      uint32_t entity_capacity, uint32_t capacity);
bool health_pool_add(HealthPool* pool, EntityId entity,
                     int32_t current, int32_t maximum, uint32_t damage_cooldown);
bool health_pool_remove(HealthPool* pool, EntityId entity);
bool health_pool_has(const HealthPool* pool, EntityId entity);
bool health_pool_get(HealthPool* pool, EntityId entity, HealthView* view);
