#include "render/renderer.h"
#include "render_handle_table.h"
#include "renderer_null_test.h"
#include "render_submission_guard.h"

#include <cstdlib>
#include <cstring>

static const uint32_t NULL_MAX_MESHES = 256;
static const uint32_t NULL_MAX_TEXTURES = 256;
static const uint32_t NULL_MAX_MATERIALS = 512;
static const uint32_t NULL_MAX_DRAWS = 4096;

struct Renderer {
    alignas(16) uint8_t handle_memory[sizeof(RenderHandleSlot) *
        (NULL_MAX_MESHES + NULL_MAX_TEXTURES + NULL_MAX_MATERIALS) + 256];
    Arena handle_arena;
    RenderHandleTable meshes;
    RenderHandleTable textures;
    RenderHandleTable materials;
    TextureHandle material_textures[NULL_MAX_MATERIALS];
    RendererStats stats;
    uint64_t frame_count;
    uint32_t draw_count;
    bool frame_begun;
};

Renderer* renderer_create(PlatformWindow* window) {
    (void)window;
    Renderer* r = (Renderer*)std::calloc(1, sizeof(Renderer));
    if (!r) return nullptr;
    arena_init_fixed(&r->handle_arena, r->handle_memory, sizeof(r->handle_memory));
    if (!render_handle_table_init(&r->meshes, &r->handle_arena, NULL_MAX_MESHES) ||
        !render_handle_table_init(&r->textures, &r->handle_arena, NULL_MAX_TEXTURES) ||
        !render_handle_table_init(&r->materials, &r->handle_arena, NULL_MAX_MATERIALS)) {
        std::free(r);
        return nullptr;
    }
    return r;
}

void renderer_destroy(Renderer* r) { std::free(r); }

MeshHandle renderer_create_mesh(Renderer* r, const MeshDesc* desc) {
    MeshHandle out{HANDLE_NULL};
    if (r && desc && desc->vertices && desc->indices && desc->vertex_count && desc->index_count)
        out.h = render_handle_alloc(&r->meshes);
    return out;
}

TextureHandle renderer_create_texture(Renderer* r, const TextureDesc* desc) {
    TextureHandle out{HANDLE_NULL};
    if (r && desc && desc->pixels && desc->width && desc->height)
        out.h = render_handle_alloc(&r->textures);
    return out;
}

MaterialHandle renderer_create_material(Renderer* r, const MaterialDesc* desc) {
    MaterialHandle out{HANDLE_NULL};
    if (!r || !desc || !render_handle_valid(&r->textures, desc->albedo.h)) return out;
    out.h = render_handle_alloc(&r->materials);
    if (!handle_is_null(out.h)) r->material_textures[handle_index(out.h)] = desc->albedo;
    return out;
}

static uint64_t null_retire_frame(const Renderer* r, uint32_t delay) {
    return r->frame_count + (delay < 2 ? 2 : delay);
}
void renderer_destroy_mesh(Renderer* r, MeshHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->meshes, h.h, null_retire_frame(r, delay));
}
void renderer_destroy_texture(Renderer* r, TextureHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->textures, h.h, null_retire_frame(r, delay));
}
void renderer_destroy_material(Renderer* r, MaterialHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->materials, h.h, null_retire_frame(r, delay));
}

bool renderer_begin_frame(Renderer* r, const FrameView* view, int width, int height, bool minimized) {
    (void)width; (void)height; (void)minimized;
    if (!r || !view || r->frame_begun) return false;
    r->frame_begun = true;
    r->draw_count = 0;
    return true;
}

bool renderer_submit(Renderer* r, const DrawItem* items, uint32_t count) {
    if (!r || !r->frame_begun ||
        !render_submission_preflight(items, count, r->draw_count, NULL_MAX_DRAWS))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!render_handle_valid(&r->meshes, items[i].mesh.h) ||
            !render_handle_valid(&r->materials, items[i].material.h)) return false;
        const TextureHandle texture = r->material_textures[handle_index(items[i].material.h)];
        if (!render_handle_valid(&r->textures, texture.h)) return false;
    }
    r->draw_count += count;
    return true;
}

void renderer_end_frame(Renderer* r) {
    if (!r || !r->frame_begun) return;
    r->frame_begun = false;
    r->stats.submitted_objects = r->draw_count;
    r->stats.scene_batches = r->draw_count ? 1u : 0u;
    r->stats.scene_draw_calls = 0;
    r->stats.total_draw_calls = 0;
    ++r->frame_count;
    render_handle_collect(&r->materials, r->frame_count, nullptr, nullptr);
    render_handle_collect(&r->textures, r->frame_count, nullptr, nullptr);
    render_handle_collect(&r->meshes, r->frame_count, nullptr, nullptr);
}

bool renderer_end_frame_capture(Renderer* r, Allocator alloc, RendererCapture* out) {
    (void)alloc; (void)out;
    renderer_end_frame(r);
    return false;
}

RendererStats renderer_get_stats(const Renderer* r) { return r ? r->stats : RendererStats{}; }
void dbg_line(Renderer*, mm::vec3, mm::vec3, uint32_t) {}
void dbg_sphere(Renderer*, mm::vec3, float, uint32_t) {}
void dbg_aabb(Renderer*, mm::vec3, mm::vec3, uint32_t) {}
void dbg_text_2d(Renderer*, float, float, float, uint32_t, const char*) {}

uint32_t renderer_null_test_draw_capacity(void) { return NULL_MAX_DRAWS; }

uint32_t renderer_null_test_draw_count(const Renderer* r) {
    return r ? r->draw_count : UINT32_MAX;
}

bool renderer_null_test_set_draw_count(Renderer* r, uint32_t count) {
    if (!r) return false;
    r->draw_count = count;
    return true;
}
