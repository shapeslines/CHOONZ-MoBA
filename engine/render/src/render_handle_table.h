#pragma once

#include <stdint.h>
#include "core/handle.h"
#include "core/mem.h"

typedef struct RenderHandleSlot {
    uint32_t generation;
    uint32_t next_free;
    uint64_t retire_frame;
    bool     alive;
    bool     retiring;
} RenderHandleSlot;

typedef struct RenderHandleTable {
    RenderHandleSlot* slots;
    uint32_t          capacity;
    uint32_t          free_head;
    uint32_t          live_count;
} RenderHandleTable;

typedef void (*RenderReleaseSlotFn)(void* user, uint32_t index);

bool   render_handle_table_init(RenderHandleTable* table, Arena* arena, uint32_t capacity);
Handle render_handle_alloc(RenderHandleTable* table);
bool   render_handle_valid(const RenderHandleTable* table, Handle handle);
bool   render_handle_retire(RenderHandleTable* table, Handle handle, uint64_t retire_frame);
void   render_handle_collect(RenderHandleTable* table, uint64_t current_frame,
                             RenderReleaseSlotFn release, void* release_user);
void   render_handle_release_all(RenderHandleTable* table,
                                 RenderReleaseSlotFn release, void* release_user);
