#pragma once
#include <stdint.h>
#include "core/mem.h"   // Allocator (capture output is caller-allocated)
#include "math/math.h"  // mm::mat4 — the seam speaks eng_math (ADR-0006: render -> math)
// The renderer seam (ADR-0004). The app/game sees ONLY this header — never
// <vulkan/vulkan.h>. M2.1: graphics pipeline + first triangle over dynamic
// rendering/synchronization2 (ADR-0012 minimum spec), offline SPIR-V (ADR-0008),
// an on-disk pipeline cache, and a readback capture for screenshots/tests.
// M2.3: per-frame view/proj UBO at set=0, fed through renderer_set_view_proj.

typedef struct PlatformWindow PlatformWindow;
typedef struct Renderer Renderer;

// Bring up Vulkan for `window`. Returns NULL if unavailable — the null backend (no
// Vulkan SDK at build time), a runtime failure, or a device below the ADR-0012
// minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2). Already logged;
// caller continues without a renderer.
Renderer* renderer_create(PlatformWindow* window);
void      renderer_destroy(Renderer* r);

// Render one frame: clear to an animated color, draw the registered pipelines (the
// M2.1 triangle, plus the M2.2 textured quad once a texture is uploaded), present.
// Pass the current framebuffer size + minimized flag from the platform pump; swapchain
// recreation on resize is handled inside. No-op for the null backend or while minimized.
void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized);

// M2.2 (PROVISIONAL until the M2.5 upload-API unification into typed handles): upload
// the quad's texture. `rgba8` is w*h*4, rows top-down, interpreted as sRGB. Copies
// through a staging buffer into a DEVICE_LOCAL image and blocks until the upload
// completes (startup-path; not for per-frame use). Replaces any previous texture
// (device-idles first). The quad draws only after a successful upload. False (logged)
// on the null backend or any failure.
bool renderer_upload_texture(Renderer* r, int width, int height, const void* rgba8);

// M2.3: hand the camera in as view/proj matrices (composed by the app with eng_math;
// the renderer never owns camera concepts). Copies both; the per-frame set=0 UBO is
// written from these at the start of every frame, so a non-moving camera costs one
// memcpy per frame. Identity until first set (the quad then draws in raw NDC).
void renderer_set_view_proj(Renderer* r, const mm::mat4* view, const mm::mat4* proj);

// Render one frame AND read its pixels back (slow path — screenshots/visual tests).
// On success fills `out` with a w*h*4 RGBA8 image (rows top-down) allocated from
// `alloc`. Fails (false, logged) on the null backend, while minimized, when the
// swapchain can't be a transfer source, or on a transient acquire failure — callers
// may simply retry next frame.
typedef struct RendererCapture {
    int      width, height;
    uint8_t* rgba8;
} RendererCapture;
bool renderer_capture(Renderer* r, int fb_width, int fb_height, bool minimized,
                      Allocator alloc, RendererCapture* out);
