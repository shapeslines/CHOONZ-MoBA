#include "render_batch.h"

#include <cstdlib>
#include <cstring>

typedef struct SortItem {
    DrawItem item;
    uint32_t pipeline_key;
    uint32_t ordinal;
} SortItem;

static int compare_sort_items(const void* lhs_ptr, const void* rhs_ptr) {
    const SortItem* lhs = (const SortItem*)lhs_ptr;
    const SortItem* rhs = (const SortItem*)rhs_ptr;
    if (lhs->pipeline_key != rhs->pipeline_key)
        return lhs->pipeline_key < rhs->pipeline_key ? -1 : 1;
    if (lhs->item.material.h != rhs->item.material.h)
        return lhs->item.material.h < rhs->item.material.h ? -1 : 1;
    if (lhs->item.mesh.h != rhs->item.mesh.h)
        return lhs->item.mesh.h < rhs->item.mesh.h ? -1 : 1;
    if (lhs->ordinal != rhs->ordinal)
        return lhs->ordinal < rhs->ordinal ? -1 : 1;
    return 0;
}
bool render_build_batches(const DrawItem* items, uint32_t count, uint32_t capacity,
                          Arena* arena, RenderResolveDrawFn resolve, void* resolve_user,
                          RenderBatchOutput* out) {
    if (!arena || !resolve || !out || (count > 0 && !items) || count > capacity)
        return false;

    std::memset(out, 0, sizeof(*out));
    if (count == 0)
        return true;

    TempMemory temporary = temp_begin(arena);
    SortItem* sorted = ARENA_PUSH_ARRAY(arena, SortItem, count);
    RenderInstanceData* instances = ARENA_PUSH_ARRAY(arena, RenderInstanceData, count);
    RenderBatch* batches = ARENA_PUSH_ARRAY(arena, RenderBatch, count);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pipeline_key = 0;
        if (!resolve(resolve_user, &items[i], &pipeline_key)) {
            temp_end(temporary);
            return false;
        }
        sorted[i].item = items[i];
        sorted[i].pipeline_key = pipeline_key;
        sorted[i].ordinal = i;
    }
    std::qsort(sorted, count, sizeof(SortItem), compare_sort_items);

    uint32_t batch_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const SortItem* src = &sorted[i];
        instances[i].model = src->item.model;

        bool starts_batch = batch_count == 0;
        if (!starts_batch) {
            const RenderBatch* prior = &batches[batch_count - 1];
            starts_batch = prior->pipeline_key != src->pipeline_key ||
                           prior->material.h != src->item.material.h ||
                           prior->mesh.h != src->item.mesh.h;
        }
        if (starts_batch) {
            RenderBatch* batch = &batches[batch_count++];
            batch->pipeline_key = src->pipeline_key;
            batch->mesh = src->item.mesh;
            batch->material = src->item.material;
            batch->instance_base = i;
            batch->instance_count = 1;
        } else {
            ++batches[batch_count - 1].instance_count;
        }
    }

    out->instances = instances;
    out->batches = batches;
    out->instance_count = count;
    out->batch_count = batch_count;
    return true;
}
