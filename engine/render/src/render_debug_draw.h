#pragma once

#include <stdint.h>
#include "core/mem.h"
#include "math/math.h"

typedef struct RenderDebugVertex {
    mm::vec3 position;
    uint32_t color_rgba8;
} RenderDebugVertex;

typedef struct RenderDebugList {
    RenderDebugVertex* vertices;
    uint32_t count;
    uint32_t capacity;
    uint32_t world_count;
} RenderDebugList;

bool render_debug_begin(RenderDebugList* list, Arena* frame_arena, uint32_t capacity);
bool render_debug_line(RenderDebugList* list, mm::vec3 a, mm::vec3 b, uint32_t color_rgba8);
bool render_debug_aabb(RenderDebugList* list, mm::vec3 min_corner, mm::vec3 max_corner,
                       uint32_t color_rgba8);
bool render_debug_sphere(RenderDebugList* list, mm::vec3 center, float radius,
                         uint32_t color_rgba8, uint32_t segments);
void render_debug_end_world(RenderDebugList* list);
bool render_debug_text_2d(RenderDebugList* list, float x, float y, float scale,
                          uint32_t color_rgba8, const char* text);
