#pragma once
#include <stdint.h>
#include "core/mem.h"   // Allocator (capture output is caller-allocated)
#include "render/renderer_types.h"
#include "math/math.h"  // mm::mat4 — the seam speaks eng_math (ADR-0006: render -> math)
// The renderer seam (ADR-0004). The app/game sees ONLY this header — never
// <vulkan/vulkan.h>. M2.1: graphics pipeline + first triangle over dynamic
// rendering/synchronization2 (ADR-0012 minimum spec), offline SPIR-V (ADR-0008),
// an on-disk pipeline cache, and a readback capture for screenshots/tests.
// M2.3–M2.5: typed resources, batched frame submission, debug draw, and capture.

typedef struct PlatformWindow PlatformWindow;
typedef struct Renderer Renderer;

// Bring up Vulkan for `window`. Returns NULL if unavailable — the null backend (no
// Vulkan SDK at build time), a runtime failure, or a device below the ADR-0012
// minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2). Already logged;
// caller continues without a renderer.
Renderer* renderer_create(PlatformWindow* window);
void      renderer_destroy(Renderer* r);

MeshHandle     renderer_create_mesh(Renderer* r, const MeshDesc* desc);
TextureHandle  renderer_create_texture(Renderer* r, const TextureDesc* desc);
MaterialHandle renderer_create_material(Renderer* r, const MaterialDesc* desc);
void renderer_destroy_mesh(Renderer* r, MeshHandle handle, uint32_t frames_until_free);
void renderer_destroy_texture(Renderer* r, TextureHandle handle, uint32_t frames_until_free);
void renderer_destroy_material(Renderer* r, MaterialHandle handle, uint32_t frames_until_free);

// Frame submission is transactional: begin resets the current frame arena, submit
// copies per-object items into it, and end sorts/coalesces and presents exactly once.
bool renderer_begin_frame(Renderer* r, const FrameView* view,
                          int fb_width, int fb_height, bool minimized);
bool renderer_submit(Renderer* r, const DrawItem* items, uint32_t count);
void renderer_end_frame(Renderer* r);

// Render one frame AND read its pixels back (slow path — screenshots/visual tests).
// On success fills `out` with a w*h*4 RGBA8 image (rows top-down) allocated from
// `alloc`. Fails (false, logged) on the null backend, while minimized, when the
// swapchain can't be a transfer source, or on a transient acquire failure — callers
// may simply retry next frame.
typedef struct RendererCapture {
    int      width, height;
    uint8_t* rgba8;
} RendererCapture;
bool renderer_end_frame_capture(Renderer* r, Allocator alloc, RendererCapture* out);
RendererStats renderer_get_stats(const Renderer* r);

void dbg_line(Renderer* r, mm::vec3 a, mm::vec3 b, uint32_t color_rgba8);
void dbg_sphere(Renderer* r, mm::vec3 center, float radius, uint32_t color_rgba8);
void dbg_aabb(Renderer* r, mm::vec3 min_corner, mm::vec3 max_corner, uint32_t color_rgba8);
void dbg_text_2d(Renderer* r, float x, float y, float scale,
                 uint32_t color_rgba8, const char* text);
