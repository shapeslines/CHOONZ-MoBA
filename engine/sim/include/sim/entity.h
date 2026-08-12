#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/handle.h"
#include "core/mem.h"

// M3.1 simulation entity lifecycle. Identity and component storage are separate:
// the manager owns generational liveness, while sparse-set pools own component
// membership. All storage is fixed-capacity and supplied by the simulation arena.
typedef struct EntityManager {
    uint16_t* generations;
    uint8_t* liveness;
    uint32_t* free_stack;
    uint32_t capacity;
    uint32_t live_count;
    uint32_t next_fresh;
    uint32_t free_count;
} EntityManager;

// Returns a base-alignment-independent upper bound. Zero means the capacity cannot
// be represented by the ADR-0003 handle index field.
size_t entity_manager_memory_required(uint32_t capacity);

// Failure is atomic: manager contents and arena offset are unchanged.
bool entity_manager_init(EntityManager* manager, Arena* arena, uint32_t capacity);

// Fresh indices ascend from zero. Released indices are reused in LIFO order.
// Exhaustion returns EntityId{HANDLE_NULL} without mutation.
EntityId entity_manager_create(EntityManager* manager);
bool entity_manager_is_alive(const EntityManager* manager, EntityId entity);
bool entity_manager_release(EntityManager* manager, EntityId entity);
