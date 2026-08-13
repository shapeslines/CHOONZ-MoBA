// tools/sandbox — engine bring-up shell: open a Win32 window, run a clean
// non-blocking message loop, render via the renderer seam, exit cleanly.
//   sandbox                       exits when you close the window (or press Esc)
//   sandbox --frames N            auto-quits after N frames (smoke test)
//   sandbox --screenshot out.bmp  captures the LAST frame to a 24-bit BMP (M2.1
//                                 readback — session-independent visual proof)
//   sandbox --orbit               auto-rotates the camera (screenshot verification)
// Loads assets/uv_test.tga directly (the asset manager arrives in Phase 4) and creates
// typed mesh/texture/material resources for the instanced cube field.
// M2.3: an orbit camera (arrows rotate, wheel zooms) feeds view/proj into the
// renderer's per-frame set=0 UBO through the seam.
#include "platform/platform.h"
#include "render/renderer.h"
#include "math/math.h"
#include "tga_direct.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

struct SandboxVertex { float x, y, z, u, v; };
static const SandboxVertex CUBE_VERTICES[24] = {
    {-0.5f,-0.5f, 0.5f,0,1},{ 0.5f,-0.5f, 0.5f,1,1},{ 0.5f, 0.5f, 0.5f,1,0},{-0.5f, 0.5f, 0.5f,0,0},
    { 0.5f,-0.5f,-0.5f,0,1},{-0.5f,-0.5f,-0.5f,1,1},{-0.5f, 0.5f,-0.5f,1,0},{ 0.5f, 0.5f,-0.5f,0,0},
    { 0.5f,-0.5f, 0.5f,0,1},{ 0.5f,-0.5f,-0.5f,1,1},{ 0.5f, 0.5f,-0.5f,1,0},{ 0.5f, 0.5f, 0.5f,0,0},
    {-0.5f,-0.5f,-0.5f,0,1},{-0.5f,-0.5f, 0.5f,1,1},{-0.5f, 0.5f, 0.5f,1,0},{-0.5f, 0.5f,-0.5f,0,0},
    {-0.5f, 0.5f, 0.5f,0,1},{ 0.5f, 0.5f, 0.5f,1,1},{ 0.5f, 0.5f,-0.5f,1,0},{-0.5f, 0.5f,-0.5f,0,0},
    {-0.5f,-0.5f,-0.5f,0,1},{ 0.5f,-0.5f,-0.5f,1,1},{ 0.5f,-0.5f, 0.5f,1,0},{-0.5f,-0.5f, 0.5f,0,0},
};
static const uint16_t CUBE_INDICES[36] = {
     0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7,  8, 9,10, 8,10,11,
    12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23,
};

// Minimal 24-bit bottom-up BMP, same shape tools/visualize writes. `rgba8` rows are
// top-down (end-frame capture contract); BMP wants bottom-up BGR with 4-byte row pad.
static bool write_bmp24(const char* path, const uint8_t* rgba8, int w, int h) {
    if (!rgba8 || w <= 0 || h <= 0) return false;
    const uint32_t row_bytes = ((uint32_t)w * 3u + 3u) & ~3u;
    const uint32_t pixel_bytes = row_bytes * (uint32_t)h;
    const uint32_t off = 14 + 40;

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    uint8_t hdr[14 + 40] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t file_size = off + pixel_bytes;
    std::memcpy(hdr + 2, &file_size, 4);
    std::memcpy(hdr + 10, &off, 4);
    uint32_t info_size = 40; std::memcpy(hdr + 14, &info_size, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixel_bytes, 4);
    bool ok = std::fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);

    uint8_t* row = (uint8_t*)std::calloc(row_bytes, 1);
    if (!row) { std::fclose(f); return false; }
    for (int y = h - 1; ok && y >= 0; --y) {            // BMP rows are bottom-up
        const uint8_t* src = rgba8 + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2];            // B
            row[x * 3 + 1] = src[x * 4 + 1];            // G
            row[x * 3 + 2] = src[x * 4 + 0];            // R
        }
        ok = std::fwrite(row, 1, row_bytes, f) == row_bytes;
    }
    std::free(row);
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    int max_frames = -1;
    const char* screenshot_path = nullptr;
    bool orbit = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot_path = argv[++i];
        else if (std::strcmp(argv[i], "--orbit") == 0)
            orbit = true;
    }

    PlatformWindowDesc desc;
    desc.title = "MOBA - sandbox (Phase 2 complete; F1 overlay)";
    desc.width = 1280; desc.height = 720;
    desc.resizable = true; desc.fullscreen = false;

    PlatformWindow* win = platform_window_open(&desc);
    if (!win) { std::printf("sandbox: failed to open window\n"); return 1; }

    Renderer* rnd = renderer_create(win);
    if (!rnd) std::printf("sandbox: renderer unavailable (null backend / no Vulkan)\n");
    MeshHandle mesh{HANDLE_NULL};
    TextureHandle texture{HANDLE_NULL};
    MaterialHandle material{HANDLE_NULL};
    if (rnd) {
        MeshDesc mesh_desc{};
        mesh_desc.vertices = CUBE_VERTICES;
        mesh_desc.vertex_count = 24;
        mesh_desc.vertex_stride = sizeof(SandboxVertex);
        mesh_desc.vertex_layout = RENDERER_VERTEX_POS3_UV2;
        mesh_desc.indices = CUBE_INDICES;
        mesh_desc.index_count = 36;
        mesh_desc.index_type = RENDERER_INDEX_U16;
        mesh = renderer_create_mesh(rnd, &mesh_desc);
    }

    // Direct TGA -> typed texture/material. Missing/bad/oversized input is non-fatal.
    if (rnd) {
        // The arena holds BOTH the raw file AND its decoded RGBA8 at once. A 24bpp TGA
        // (3 B/px) decodes to 4 B/px, so worst-case live bytes ~= 7/3 * file. Cap the
        // file at 3/8 of the arena (< 3/7, with slack) so a valid-but-huge TGA is
        // rejected here rather than overrunning the arena's hard abort (platform.h:
        // an on-disk size must never be able to trigger it).
        const size_t TEX_ARENA_BYTES = 64u * 1024 * 1024;
        const size_t TGA_FILE_CAP    = TEX_ARENA_BYTES / 8 * 3;   // ~24 MiB; 24 + 4/3*24 = 56 < 64
        Arena tex_arena;
        if (platform_arena_reserve(&tex_arena, TEX_ARENA_BYTES)) {
            char tga_path[512];
            std::snprintf(tga_path, sizeof(tga_path), "%s/uv_test.tga", MOBA_ASSET_DIR);
            PlatformFile file = {};
            TgaImage img = {};
            size_t tga_size = 0;
            if (!platform_file_size(tga_path, &tga_size) || tga_size > TGA_FILE_CAP)
                std::printf("sandbox: texture missing or oversized: %s\n", tga_path);
            else if (!platform_file_read(tga_path, arena_allocator(&tex_arena), &file))
                std::printf("sandbox: texture unreadable: %s\n", tga_path);
            else if (!tga_decode(file.data, file.size, arena_allocator(&tex_arena), &img))
                std::printf("sandbox: %s is not a supported TGA\n", tga_path);
            else {
                TextureDesc texture_desc{};
                texture_desc.pixels = img.rgba8;
                texture_desc.width = (uint32_t)img.width;
                texture_desc.height = (uint32_t)img.height;
                texture_desc.format = RENDERER_TEXTURE_RGBA8_SRGB;
                texture = renderer_create_texture(rnd, &texture_desc);
                MaterialDesc material_desc{};
                material_desc.albedo = texture;
                material_desc.sampler = RENDERER_SAMPLER_LINEAR_REPEAT;
                material = renderer_create_material(rnd, &material_desc);
                if (!handle_is_null(material.h))
                    std::printf("sandbox: typed cube material loaded (%dx%d from uv_test.tga)\n", img.width, img.height);
            }
            platform_arena_release(&tex_arena);
        }
    }

    const uint64_t freq = platform_time_frequency();
    uint64_t prev  = platform_time_ticks();
    uint64_t start = prev;
    PlatformFrameInput in;
    long frame = 0;
    double since_print = 0.0;
    bool quit_requested = false;
    bool overlay_visible = true;
    bool f1_was_down = false;
    int32_t last_width = desc.width;
    int32_t last_height = desc.height;
    bool last_focused = true;
    bool last_minimized = false;

    DrawItem cubes[500]{};
    for (uint32_t z = 0, i = 0; z < 25; ++z) {
        for (uint32_t x = 0; x < 20; ++x, ++i) {
            const float px = ((float)x - 9.5f) * 1.2f;
            const float pz = ((float)z - 12.0f) * 1.2f;
            cubes[i].model = mm::mat4_trs(mm::vec3_make(px, 0.5f, pz), mm::quat_identity(), mm::vec3_splat(0.8f));
            cubes[i].mesh = mesh;
            cubes[i].material = material;
        }
    }

    // M2.3 orbit camera: yaw/pitch/distance around the world origin. Arrows rotate,
    // wheel zooms; --orbit auto-rotates for scripted screenshot verification.
    double cam_yaw = 0.0, cam_pitch = 0.65, cam_dist = 32.0;
    const mm::vec3 cam_target = mm::vec3_make(0, 0, 0);
    const mm::vec3 world_up   = mm::vec3_make(0, 1, 0);

    std::printf("sandbox: window open (%dx%d). %s\n", desc.width, desc.height,
                max_frames >= 0 ? "auto-quit mode" : "close the window or press Esc to exit");

    while (platform_pump_events(win, &in)) {
        uint64_t now = platform_time_ticks();
        double dt = (double)(now - prev) / (double)freq;
        prev = now;
        ++frame;

        if (in.fb_width != last_width || in.fb_height != last_height) {
            std::printf("sandbox: event resize %dx%d -> %dx%d\n",
                        last_width, last_height, in.fb_width, in.fb_height);
            last_width = in.fb_width;
            last_height = in.fb_height;
        }
        if (in.window_focused != last_focused) {
            std::printf("sandbox: event focus %s\n", in.window_focused ? "gained" : "lost");
            last_focused = in.window_focused;
        }
        if (in.window_minimized != last_minimized) {
            std::printf("sandbox: event window %s\n", in.window_minimized ? "minimized" : "restored");
            last_minimized = in.window_minimized;
        }

        since_print += dt;
        if (since_print >= 0.25) {   // ~4 status lines/sec
            std::printf("  frame %ld  dt=%.2fms  size=%dx%d  focus=%d  min=%d  cam=(%.2f, %.2f, %.2f)\n",
                        frame, dt * 1000.0, in.fb_width, in.fb_height,
                        (int)in.window_focused, (int)in.window_minimized, cam_yaw, cam_pitch, cam_dist);
            since_print = 0.0;
        }

        if (in.keyboard.down[KEY_ESCAPE]) { std::printf("  Esc -> quit\n"); quit_requested = true; }
        if (in.keyboard.down[KEY_F1] && !f1_was_down) {
            overlay_visible = !overlay_visible;
            std::printf("sandbox: event F1 overlay %s\n", overlay_visible ? "on" : "off");
        }
        f1_was_down = in.keyboard.down[KEY_F1];
        if (max_frames >= 0 && frame >= max_frames) {
            std::printf("  reached %d frames -> quit\n", max_frames);
            quit_requested = true;
        }

        // Camera input (M2.3): arrows = yaw/pitch, wheel = distance. --orbit advances
        // a fixed PER-FRAME yaw step (render-rate independent, so --frames N lands on
        // a deterministic camera angle for screenshot verification).
        if (orbit) {
            cam_yaw += 0.01;
        } else {
            if (in.keyboard.down[KEY_LEFT])  cam_yaw   += dt * 1.2;
            if (in.keyboard.down[KEY_RIGHT]) cam_yaw   -= dt * 1.2;
            if (in.keyboard.down[KEY_UP])    cam_pitch += dt * 0.8;
            if (in.keyboard.down[KEY_DOWN])  cam_pitch -= dt * 0.8;
        }
        if (in.mouse.wheel != 0)
            cam_dist *= 1.0 - (double)in.mouse.wheel * 0.1;
        cam_pitch = cam_pitch > 1.55 ? 1.55 : (cam_pitch < -1.55 ? -1.55 : cam_pitch);
        cam_dist  = cam_dist  > 60.0 ? 60.0 : (cam_dist  < 2.0 ? 2.0 : cam_dist);

        // Compose view/proj with eng_math and hand them through the seam (M2.3).
        if (rnd) {
            const double sy = std::sin(cam_yaw), cy = std::cos(cam_yaw);
            const double sp = std::sin(cam_pitch), cp = std::cos(cam_pitch);
            mm::vec3 eye = mm::vec3_make((float)(cam_dist * cp * sy),
                                         (float)(cam_dist * sp),
                                         (float)(cam_dist * cp * cy));
            mm::mat4 view = mm::mat4_look_at_rh(eye, cam_target, world_up);
            float aspect = (in.fb_height > 0) ? (float)in.fb_width / (float)in.fb_height : 16.0f / 9.0f;
            mm::mat4 proj = mm::mat4_perspective_vk(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
            FrameView frame_view{};
            frame_view.view = view;
            frame_view.proj = proj;
            frame_view.camera_pos = eye;
            frame_view.time_seconds = (float)((double)(now - start) / (double)freq);
            frame_view.delta_seconds = (float)dt;
            if (renderer_begin_frame(rnd, &frame_view, in.fb_width, in.fb_height, in.window_minimized)) {
                renderer_submit(rnd, cubes, 500);
                dbg_aabb(rnd, mm::vec3_make(-12.0f, 0.0f, -15.0f), mm::vec3_make(12.0f, 1.0f, 15.0f), 0xff00ffffu);
                dbg_sphere(rnd, mm::vec3_make(0, 2, 0), 2.0f, 0xff00ff00u);
                if (overlay_visible) {
                    RendererStats stats = renderer_get_stats(rnd);
                    char overlay[256];
                    std::snprintf(overlay, sizeof(overlay),
                                  "FPS: %.1f\nFRAME: %.2f MS\nFRAME ARENA: %zu B\nPERSIST: %zu B\nALLOCS: %u\nDRAWS: %u",
                                  dt > 0.0 ? 1.0 / dt : 0.0, dt * 1000.0,
                                  stats.frame_arena_bytes, stats.persistent_arena_bytes,
                                  stats.live_device_allocations,
                                  stats.total_draw_calls);
                    dbg_text_2d(rnd, 16.0f, 16.0f, 2.0f, 0xffffffffu, overlay);
                }
            }
        }

        if (quit_requested && screenshot_path && rnd) {
            // Last frame: render once more WITH readback and dump it. A transient
            // capture failure (acquire OUT_OF_DATE etc.) just exits without a BMP.
            Arena shot_arena;
            if (platform_arena_reserve(&shot_arena, 64u * 1024 * 1024)) {
                RendererCapture cap;
                if (renderer_end_frame_capture(rnd, arena_allocator(&shot_arena), &cap)) {
                    if (write_bmp24(screenshot_path, cap.rgba8, cap.width, cap.height))
                        std::printf("  screenshot: %s (%dx%d)\n", screenshot_path, cap.width, cap.height);
                    else
                        std::printf("  screenshot: BMP write FAILED (%s)\n", screenshot_path);
                } else {
                    std::printf("  screenshot: capture FAILED\n");
                }
                platform_arena_release(&shot_arena);
            }
        }
        if (quit_requested) break;

        if (rnd) renderer_end_frame(rnd);  // vsync paces us
        else     platform_sleep_ms(6);   // no renderer: don't busy-spin
    }

    double elapsed = (double)(platform_time_ticks() - start) / (double)freq;
    if (rnd) {
        RendererStats stats = renderer_get_stats(rnd);
        std::printf("sandbox: renderer stats objects=%u batches=%u scene_draws=%u total_draws=%u allocs=%u\n",
                    stats.submitted_objects, stats.scene_batches, stats.scene_draw_calls,
                    stats.total_draw_calls, stats.live_device_allocations);
    }
    renderer_destroy_material(rnd, material, 0);
    renderer_destroy_texture(rnd, texture, 0);
    renderer_destroy_mesh(rnd, mesh, 0);
    renderer_destroy(rnd);
    platform_window_close(win);
    std::printf("sandbox: clean exit after %ld frames (%.2fs)\n", frame, elapsed);
    return 0;
}
