#include "sim/entity.h"

#include "core/assert.h"

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

size_t entity_manager_memory_required(uint32_t capacity) {
    if (capacity == 0u || capacity > HANDLE_INDEX_MASK + 1u) return 0u;

    // One maximum padding allowance per arena allocation makes this sufficient
    // regardless of the caller's base address and current offset.
    size_t generation_bytes = sizeof(uint16_t) * static_cast<size_t>(capacity);
    size_t liveness_bytes = sizeof(uint8_t) * static_cast<size_t>(capacity);
    size_t free_stack_bytes = sizeof(uint32_t) * static_cast<size_t>(capacity);
    return generation_bytes + (alignof(uint16_t) - 1u) +
           liveness_bytes + (alignof(uint8_t) - 1u) +
           free_stack_bytes + (alignof(uint32_t) - 1u);
}

bool entity_manager_init(EntityManager* manager, Arena* arena, uint32_t capacity) {
    if (!manager || !arena || capacity == 0u || capacity > HANDLE_INDEX_MASK + 1u ||
        !arena->base || arena->offset > arena->reserved) return false;

    size_t end = arena->offset;
    if (!layout_advance(arena, &end, sizeof(uint16_t) * static_cast<size_t>(capacity),
                        alignof(uint16_t)) ||
        !layout_advance(arena, &end, sizeof(uint8_t) * static_cast<size_t>(capacity),
                        alignof(uint8_t)) ||
        !layout_advance(arena, &end, sizeof(uint32_t) * static_cast<size_t>(capacity),
                        alignof(uint32_t))) return false;

    EntityManager staged{};
    staged.generations = static_cast<uint16_t*>(arena_push_zero(
        arena, sizeof(uint16_t) * static_cast<size_t>(capacity), alignof(uint16_t)));
    staged.liveness = static_cast<uint8_t*>(arena_push_zero(
        arena, sizeof(uint8_t) * static_cast<size_t>(capacity), alignof(uint8_t)));
    staged.free_stack = static_cast<uint32_t*>(arena_push_zero(
        arena, sizeof(uint32_t) * static_cast<size_t>(capacity), alignof(uint32_t)));
    staged.capacity = capacity;
    *manager = staged;
    return true;
}

EntityId entity_manager_create(EntityManager* manager) {
    EntityId null_entity{HANDLE_NULL};
    if (!manager || !manager->generations || !manager->liveness || !manager->free_stack ||
        manager->capacity == 0u) return null_entity;

    uint32_t index = 0u;
    if (manager->free_count > 0u) {
        index = manager->free_stack[manager->free_count - 1u];
        if (index >= manager->capacity || manager->liveness[index] != 0u) return null_entity;
        --manager->free_count;
    } else {
        if (manager->next_fresh >= manager->capacity) return null_entity;
        index = manager->next_fresh;
        ++manager->next_fresh;
    }

    uint16_t generation = manager->generations[index];
    if (generation == 0u) generation = 1u;
    manager->generations[index] = generation;
    manager->liveness[index] = 1u;
    ++manager->live_count;
    return EntityId{handle_make(index, generation)};
}

bool entity_manager_is_alive(const EntityManager* manager, EntityId entity) {
    if (!manager || !manager->generations || !manager->liveness) return false;
    uint32_t generation = handle_gen(entity.h);
    uint32_t index = handle_index(entity.h);
    return generation != 0u && index < manager->capacity &&
           manager->liveness[index] != 0u && manager->generations[index] == generation;
}

bool entity_manager_release(EntityManager* manager, EntityId entity) {
    if (!entity_manager_is_alive(manager, entity) || !manager->free_stack ||
        manager->free_count >= manager->capacity || manager->live_count == 0u) return false;

    uint32_t index = handle_index(entity.h);
    uint32_t generation = static_cast<uint32_t>(manager->generations[index]) + 1u;
    ASSERT(generation <= HANDLE_GEN_MASK);
    if (generation > HANDLE_GEN_MASK) generation = 1u;

    manager->generations[index] = static_cast<uint16_t>(generation);
    manager->liveness[index] = 0u;
    manager->free_stack[manager->free_count] = index;
    ++manager->free_count;
    --manager->live_count;
    return true;
}
