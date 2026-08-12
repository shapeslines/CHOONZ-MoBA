#include "render/renderer.h"
// Null backend — compiled when no Vulkan SDK is present at build time (e.g. CI). The
// seam still links so the app builds and runs; it just gets no renderer. This is an
// early stand-in for the M2.5 null backend.
Renderer* renderer_create(PlatformWindow* window) { (void)window; return nullptr; }
void      renderer_destroy(Renderer* r)           { (void)r; }
void      renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized) {
    (void)r; (void)fb_width; (void)fb_height; (void)minimized;
}
bool renderer_capture(Renderer* r, int fb_width, int fb_height, bool minimized,
                      Allocator alloc, RendererCapture* out) {
    (void)r; (void)fb_width; (void)fb_height; (void)minimized; (void)alloc; (void)out;
    return false;
}
bool renderer_upload_texture(Renderer* r, int width, int height, const void* rgba8) {
    (void)r; (void)width; (void)height; (void)rgba8;
    return false;
}
void renderer_set_view_proj(Renderer* r, const mm::mat4* view, const mm::mat4* proj) {
    (void)r; (void)view; (void)proj;
}
