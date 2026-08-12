#pragma once

#include <stddef.h>
#include <stdint.h>
#include "core/handle.h"
#include "math/math.h"

// Vulkan-free renderer data contracts. These are the only resource and draw shapes
// the app/asset layers see; backend-specific objects remain private to eng_render.

typedef enum RendererVertexLayout {
    RENDERER_VERTEX_POS3_UV2 = 0,
} RendererVertexLayout;

typedef enum RendererIndexType {
    RENDERER_INDEX_U16 = 0,
    RENDERER_INDEX_U32,
} RendererIndexType;

typedef enum RendererTextureFormat {
    RENDERER_TEXTURE_RGBA8_SRGB = 0,
} RendererTextureFormat;

typedef enum RendererSampler {
    RENDERER_SAMPLER_LINEAR_REPEAT = 0,
} RendererSampler;

typedef struct MeshDesc {
    const void*          vertices;
    uint32_t             vertex_count;
    uint32_t             vertex_stride;
    RendererVertexLayout vertex_layout;
    const void*          indices;
    uint32_t             index_count;
    RendererIndexType    index_type;
} MeshDesc;

typedef struct TextureDesc {
    const void*           pixels;
    uint32_t              width;
    uint32_t              height;
    RendererTextureFormat format;
} TextureDesc;

typedef struct MaterialDesc {
    TextureHandle   albedo;
    RendererSampler sampler;
} MaterialDesc;

typedef struct FrameView {
    mm::mat4 view;
    mm::mat4 proj;
    mm::vec3 camera_pos;
    float    time_seconds;
    float    delta_seconds;
} FrameView;

// One object at the public seam. The renderer sorts these by backend pipeline,
// material, and mesh; model matrices are packed into the per-frame instance stream.
typedef struct DrawItem {
    mm::mat4       model;
    MeshHandle     mesh;
    MaterialHandle material;
    uint32_t       reserved[2]; // explicit tail padding keeps the 16-byte ABI warning-free
} DrawItem;

typedef struct RendererStats {
    uint32_t submitted_objects;
    uint32_t scene_batches;
    uint32_t scene_draw_calls;
    uint32_t total_draw_calls;
    uint32_t live_device_allocations;
    size_t   frame_arena_bytes;
    size_t   frame_arena_high_water;
    size_t   persistent_arena_bytes;
    size_t   persistent_arena_high_water;
} RendererStats;
