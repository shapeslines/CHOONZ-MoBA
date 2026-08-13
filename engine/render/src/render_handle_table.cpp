#include "render_handle_table.h"

#include <limits.h>
#include <cstring>

static const uint32_t FREE_NONE = UINT32_MAX;

bool render_handle_table_init(RenderHandleTable* table, Arena* arena, uint32_t capacity) {
    if (!table || !arena || capacity == 0 || capacity > HANDLE_INDEX_MASK + 1u)
        return false;
    std::memset(table, 0, sizeof(*table));
    table->slots = ARENA_PUSH_ARRAY(arena, RenderHandleSlot, capacity);
    table->capacity = capacity;
    table->free_head = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        table->slots[i].generation = 1;
        table->slots[i].next_free = i + 1 < capacity ? i + 1 : FREE_NONE;
    }
    return true;
}
Handle render_handle_alloc(RenderHandleTable* table) {
    if (!table || !table->slots || table->capacity == 0u ||
        table->live_count >= table->capacity || table->free_head == FREE_NONE ||
        table->free_head >= table->capacity)
        return HANDLE_NULL;
    const uint32_t index = table->free_head;
    RenderHandleSlot* slot = &table->slots[index];
    const uint32_t next = slot->next_free;
    if (slot->alive || slot->retiring || (next != FREE_NONE && next >= table->capacity))
        return HANDLE_NULL;
    table->free_head = next;
    slot->next_free = FREE_NONE;
    slot->alive = true;
    slot->retiring = false;
    slot->retire_frame = 0;
    ++table->live_count;
    return handle_make(index, slot->generation);
}

bool render_handle_valid(const RenderHandleTable* table, Handle handle) {
    if (!table || handle_is_null(handle))
        return false;
    const uint32_t index = handle_index(handle);
    if (index >= table->capacity)
        return false;
    const RenderHandleSlot* slot = &table->slots[index];
    return slot->alive && !slot->retiring && slot->generation == handle_gen(handle);
}

bool render_handle_retire(RenderHandleTable* table, Handle handle, uint64_t retire_frame) {
    if (!render_handle_valid(table, handle))
        return false;
    RenderHandleSlot* slot = &table->slots[handle_index(handle)];
    slot->alive = false;
    slot->retiring = true;
    slot->retire_frame = retire_frame;
    --table->live_count;
    return true;
}

static void release_slot(RenderHandleTable* table, uint32_t index,
                         RenderReleaseSlotFn release, void* release_user) {
    RenderHandleSlot* slot = &table->slots[index];
    if (release)
        release(release_user, index);
    slot->retiring = false;
    slot->retire_frame = 0;
    slot->generation = (slot->generation + 1u) & HANDLE_GEN_MASK;
    ENSURE_MSG(slot->generation != 0, "renderer handle generation wrapped");
    slot->next_free = table->free_head;
    table->free_head = index;
}

void render_handle_collect(RenderHandleTable* table, uint64_t current_frame,
                           RenderReleaseSlotFn release, void* release_user) {
    if (!table)
        return;
    for (uint32_t i = 0; i < table->capacity; ++i) {
        RenderHandleSlot* slot = &table->slots[i];
        if (slot->retiring && slot->retire_frame <= current_frame)
            release_slot(table, i, release, release_user);
    }
}

void render_handle_release_all(RenderHandleTable* table,
                               RenderReleaseSlotFn release, void* release_user) {
    if (!table)
        return;
    for (uint32_t i = 0; i < table->capacity; ++i) {
        RenderHandleSlot* slot = &table->slots[i];
        if (slot->alive || slot->retiring) {
            if (release)
                release(release_user, i);
            slot->alive = false;
            slot->retiring = false;
        }
    }
    table->live_count = 0;
}
