// tools/sandbox — engine bring-up shell: open a Win32 window, run a clean
// non-blocking message loop, render via the renderer seam, exit cleanly.
//   sandbox                       exits when you close the window (or press Esc)
//   sandbox --frames N            auto-quits after N frames (smoke test)
//   sandbox --screenshot out.bmp  captures the LAST frame to a 24-bit BMP (M2.1
//                                 readback — session-independent visual proof)
//   sandbox --orbit               auto-rotates the camera (screenshot verification)
// M2.2: loads assets/uv_test.tga (direct TGA — the asset manager doesn't exist yet)
// and uploads it so the renderer's textured quad draws.
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

// Minimal 24-bit bottom-up BMP, same shape tools/visualize writes. `rgba8` rows are
// top-down (renderer_capture contract); BMP wants bottom-up BGR with 4-byte row pad.
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
    desc.title = "MOBA - sandbox (M2.3)";
    desc.width = 1280; desc.height = 720;
    desc.resizable = true; desc.fullscreen = false;

    PlatformWindow* win = platform_window_open(&desc);
    if (!win) { std::printf("sandbox: failed to open window\n"); return 1; }

    Renderer* rnd = renderer_create(win);
    if (!rnd) std::printf("sandbox: renderer unavailable (null backend / no Vulkan)\n");

    // M2.2: TGA -> RGBA8 -> GPU. A missing/bad/oversized texture is non-fatal — the
    // renderer simply keeps drawing without the quad.
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
            else if (renderer_upload_texture(rnd, img.width, img.height, img.rgba8))
                std::printf("sandbox: quad texture loaded (%dx%d from uv_test.tga)\n", img.width, img.height);
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

    // M2.3 orbit camera: yaw/pitch/distance around the world origin. Arrows rotate,
    // wheel zooms; --orbit auto-rotates for scripted screenshot verification.
    double cam_yaw = 0.0, cam_pitch = 0.5, cam_dist = 8.0;
    const mm::vec3 cam_target = mm::vec3_make(0, 0, 0);
    const mm::vec3 world_up   = mm::vec3_make(0, 1, 0);

    std::printf("sandbox: window open (%dx%d). %s\n", desc.width, desc.height,
                max_frames >= 0 ? "auto-quit mode" : "close the window or press Esc to exit");

    while (platform_pump_events(win, &in)) {
        uint64_t now = platform_time_ticks();
        double dt = (double)(now - prev) / (double)freq;
        prev = now;
        ++frame;

        since_print += dt;
        if (since_print >= 0.25) {   // ~4 status lines/sec
            std::printf("  frame %ld  dt=%.2fms  size=%dx%d  focus=%d  min=%d  cam=(%.2f, %.2f, %.2f)\n",
                        frame, dt * 1000.0, in.fb_width, in.fb_height,
                        (int)in.window_focused, (int)in.window_minimized, cam_yaw, cam_pitch, cam_dist);
            since_print = 0.0;
        }

        if (in.keyboard.down[KEY_ESCAPE]) { std::printf("  Esc -> quit\n"); quit_requested = true; }
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
        cam_dist  = cam_dist  > 40.0 ? 40.0 : (cam_dist  < 2.0 ? 2.0 : cam_dist);

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
            renderer_set_view_proj(rnd, &view, &proj);
        }

        if (quit_requested && screenshot_path && rnd) {
            // Last frame: render once more WITH readback and dump it. A transient
            // capture failure (acquire OUT_OF_DATE etc.) just exits without a BMP.
            Arena shot_arena;
            if (platform_arena_reserve(&shot_arena, 64u * 1024 * 1024)) {
                RendererCapture cap;
                if (renderer_capture(rnd, in.fb_width, in.fb_height, in.window_minimized,
                                     arena_allocator(&shot_arena), &cap)) {
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

        if (rnd) renderer_draw(rnd, in.fb_width, in.fb_height, in.window_minimized);  // vsync paces us
        else     platform_sleep_ms(6);   // no renderer: don't busy-spin
    }

    double elapsed = (double)(platform_time_ticks() - start) / (double)freq;
    renderer_destroy(rnd);
    platform_window_close(win);
    std::printf("sandbox: clean exit after %ld frames (%.2fs)\n", frame, elapsed);
    return 0;
}
