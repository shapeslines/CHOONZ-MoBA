#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/handle.h"
#include "core/mem.h"

#define COMPONENT_POOL_INVALID_DENSE UINT32_MAX

// Shared sparse-set membership. Component values live in typed parallel arrays;
// this core owns only entity membership and the bidirectional index mapping.
typedef struct ComponentPool {
    uint32_t* sparse;
    EntityId* dense_entities;
    uint32_t entity_capacity;
    uint32_t capacity;
    uint32_t count;
} ComponentPool;

typedef struct ComponentPoolRemoveResult {
    uint32_t removed_dense;
    uint32_t moved_from_dense;
    EntityId moved_entity;
} ComponentPoolRemoveResult;

size_t component_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);

// Failure is atomic: pool contents and arena offset are unchanged.
bool component_pool_init(ComponentPool* pool, Arena* arena,
                         uint32_t entity_capacity, uint32_t capacity);

bool component_pool_has(const ComponentPool* pool, EntityId entity);
uint32_t component_pool_dense_index(const ComponentPool* pool, EntityId entity);

// Optional outputs are assigned only on success. Duplicate, stale, full, and
// missing operations leave both membership and caller output unchanged.
bool component_pool_add(ComponentPool* pool, EntityId entity, uint32_t* dense_index);
bool component_pool_remove(ComponentPool* pool, EntityId entity,
                           ComponentPoolRemoveResult* result);
