#pragma once

#include <stdint.h>
#include "core/mem.h"
#include "render/renderer_types.h"

typedef struct RenderInstanceData {
    mm::mat4 model;
} RenderInstanceData;

typedef struct RenderBatch {
    uint32_t       pipeline_key;
    MeshHandle     mesh;
    MaterialHandle material;
    uint32_t       instance_base;
    uint32_t       instance_count;
} RenderBatch;

typedef struct RenderBatchOutput {
    RenderInstanceData* instances;
    RenderBatch*        batches;
    uint32_t            instance_count;
    uint32_t            batch_count;
} RenderBatchOutput;

// Resolves the backend pipeline and validates both resource handles. Returning false
// rejects the whole submission rather than silently drawing a partial frame.
typedef bool (*RenderResolveDrawFn)(void* user, const DrawItem* item, uint32_t* out_pipeline_key);

bool render_build_batches(const DrawItem* items, uint32_t count, uint32_t capacity,
                          Arena* arena, RenderResolveDrawFn resolve, void* resolve_user,
                          RenderBatchOutput* out);
