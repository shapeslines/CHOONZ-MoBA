// tools/sandbox — engine bring-up shell: open a Win32 window, run a clean
// non-blocking message loop, render via the renderer seam, exit cleanly.
//   sandbox                       exits when you close the window (or press Esc)
//   sandbox --frames N            auto-quits after N frames (smoke test)
//   sandbox --screenshot out.bmp  captures the LAST frame to a 24-bit BMP (M2.1
//                                 readback — session-independent visual proof)
//   sandbox --orbit               auto-rotates the camera (screenshot verification)
//   sandbox --sim-self-check      runs the headless 10,000-tick eng_sim oracle
// Loads assets/uv_test.tga through the promoted M4.0 parser and creates typed
// mesh/texture/material resources for the instanced cube field.
// M2.3: an orbit camera (arrows rotate, wheel zooms) feeds view/proj into the
// renderer's per-frame set=0 UBO through the seam.
#include "platform/platform.h"
#include "platform/platform_fixed_step.h"
#include "render/renderer.h"
#include "math/math.h"
#include "sim/oracle.h"
#include "sim/sim.h"
#include "game/present.h"
#include "assets/assets.h"
#include <windows.h>       // SetProcessDpiAwarenessContext (G28)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>

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

static TextureHandle sandbox_asset_create_texture(void* user, const uint8_t* rgba8,
                                                   uint32_t width, uint32_t height) {
    TextureHandle invalid{HANDLE_NULL};
    Renderer* renderer = (Renderer*)user;
    if (!renderer || !rgba8 || width == 0u || height == 0u) return invalid;
    TextureDesc desc{};
    desc.pixels = rgba8;
    desc.width = width;
    desc.height = height;
    desc.format = RENDERER_TEXTURE_RGBA8_SRGB;
    return renderer_create_texture(renderer, &desc);
}

static void sandbox_asset_destroy_texture(void* user, TextureHandle texture,
                                          uint32_t frames_until_free) {
    Renderer* renderer = (Renderer*)user;
    if (renderer && !handle_is_null(texture.h))
        renderer_destroy_texture(renderer, texture, frames_until_free);
}

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

static int run_sim_self_check(void) {
    alignas(16) static uint8_t world_storage[SIM_ORACLE_WORLD_STORAGE_SIZE];
    SimOracleResult result{};
    if (!sim_oracle_run(world_storage, sizeof(world_storage), &result)) {
        std::printf("sim oracle failed\n");
        return 2;
    }
    std::printf(
        "sim_oracle ticks=%llu commands=%llu final=0x%016llx stream=0x%016llx logic=0x%016llx\n",
        static_cast<unsigned long long>(result.tick_count),
        static_cast<unsigned long long>(result.command_count),
        static_cast<unsigned long long>(result.final_state_hash),
        static_cast<unsigned long long>(result.hash_stream_digest),
        static_cast<unsigned long long>(result.logic_hash));
    return 0;
}

static bool parse_frame_count(const char* text, int* out) {
    if (!text || !out || text[0] == '\0' || text[0] == '0') return false;

    int value = 0;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        const int digit = *cursor - '0';
        if (value > (INT_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }

    *out = value;
    return true;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--sim-self-check") == 0)
        return run_sim_self_check();

    // G28: DPI awareness, before any window exists. Without it Windows scales the
    // window bitmap and overlay text + input coordinates are wrong on scaled
    // displays. SYSTEM_AWARE: one scale, physical pixels everywhere. PER_MONITOR_V2
    // (per-monitor rescale) additionally needs WM_DPICHANGED handling in the
    // platform — a documented follow-up, not silently assumed.
    int max_frames = -1;
    const char* screenshot_path = nullptr;
    bool orbit = false;
    bool saw_frames = false;
    bool saw_screenshot = false;
    bool saw_orbit = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            if (saw_frames || i + 1 >= argc ||
                !parse_frame_count(argv[i + 1], &max_frames)) {
                std::fprintf(stderr,
                             "sandbox: --frames expects a canonical positive integer\n");
                return 2;
            }
            saw_frames = true;
            ++i;
        } else if (std::strcmp(argv[i], "--screenshot") == 0) {
            if (saw_screenshot || i + 1 >= argc || argv[i + 1][0] == '\0' ||
                argv[i + 1][0] == '-') {
                std::fprintf(stderr, "sandbox: --screenshot expects one output path\n");
                return 2;
            }
            screenshot_path = argv[++i];
            saw_screenshot = true;
        } else if (std::strcmp(argv[i], "--orbit") == 0) {
            if (saw_orbit) {
                std::fprintf(stderr, "sandbox: duplicate option '--orbit'\n");
                return 2;
            }
            orbit = true;
            saw_orbit = true;
        } else {
            std::fprintf(stderr, "sandbox: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    PlatformWindowDesc desc;
    desc.title = "MOBA - sandbox (Phase 4 M4.1 cooker; F1 overlay)";
    desc.width = 1280; desc.height = 720;
    desc.resizable = true; desc.fullscreen = false;

    PlatformWindow* win = platform_window_open(&desc);
    if (!win) { std::printf("sandbox: failed to open window\n"); return 1; }

    Renderer* rnd = renderer_create(win);
    if (!rnd) std::printf("sandbox: renderer unavailable (null backend / no Vulkan)\n");
    MeshHandle mesh{HANDLE_NULL};
    TextureHandle texture{HANDLE_NULL};
    MaterialHandle material{HANDLE_NULL};
    AssetRegistry asset_registry{};
    Arena asset_persistent_arena{};
    Arena asset_level_arena{};
    Arena asset_global_arena{};
    Arena asset_io_arena{};
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

    // S4 procedural bridge: the runtime registry is already baked-only, while S5
    // wires the generated catalog/content target into this executable. The registry
    // still owns the texture handle and level lifetime; no source-file fallback exists.
    if (rnd) {
        AssetRegistryConfig asset_config = asset_registry_config_default(MOBA_ASSET_DIR);
        asset_config.capacity = 64u;
        const size_t registry_bytes = asset_registry_memory_required(asset_config);
        const size_t LEVEL_BYTES = 64u * 1024u * 1024u;
        const size_t GLOBAL_BYTES = 8u * 1024u * 1024u;
        const size_t IO_BYTES = 64u * 1024u * 1024u;
        const bool arenas_ready = registry_bytes != 0u &&
            platform_arena_reserve(&asset_persistent_arena, registry_bytes) &&
            platform_arena_reserve(&asset_level_arena, LEVEL_BYTES) &&
            platform_arena_reserve(&asset_global_arena, GLOBAL_BYTES) &&
            platform_arena_reserve(&asset_io_arena, IO_BYTES);
        AssetRendererApi asset_renderer{rnd, sandbox_asset_create_texture,
                                        sandbox_asset_destroy_texture};
        if (arenas_ready &&
            asset_registry_init(&asset_registry, &asset_persistent_arena,
                                &asset_level_arena, &asset_global_arena,
                                &asset_io_arena, asset_renderer, asset_config)) {
            static const uint8_t bridge_pixels[] = {
                255u, 255u, 255u, 255u, 32u, 32u, 32u, 255u,
                32u, 32u, 32u, 255u, 255u, 255u, 255u, 255u,
            };
            AssetTextureSource bridge_source{bridge_pixels, 2u, 2u};
            AssetHandle texture_asset = asset_register_texture(
                &asset_registry, "uv_test.tga", ASSET_LIFETIME_LEVEL,
                bridge_source);
            AssetTextureView texture_view{};
            if (asset_get_texture(&asset_registry, texture_asset, &texture_view)) {
                texture = texture_view.gpu;
                MaterialDesc material_desc{};
                material_desc.albedo = texture;
                material_desc.sampler = RENDERER_SAMPLER_LINEAR_REPEAT;
                material = renderer_create_material(rnd, &material_desc);
                if (!handle_is_null(material.h))
                    std::printf("sandbox: asset-managed cube material loaded (%ux%u id=0x%016llx)\n",
                                texture_view.width, texture_view.height,
                                (unsigned long long)texture_view.id);
            }
        } else std::printf("sandbox: asset registry initialization failed\n");
    }

    const uint64_t freq = platform_time_frequency();
    uint64_t prev  = platform_time_ticks();
    uint64_t start = prev;

    // M3.3: one deterministic SimWorld drives presentation through the present glue.
    // Units orbit the origin on deterministic per-tick SET_VELOCITY commands (pure
    // integer math — fix_sin tables — so the runtime stream is reproducible).
    Arena world_arena{};
    Arena present_arena{};
    SimWorld world{};
    PresentState present{};
    const SimWorldConfig world_config = sim_world_config_default();
    if (!platform_arena_reserve(&world_arena, sim_world_memory_required(world_config)) ||
        !sim_init(&world, &world_arena, 0x4D4F4241u, world_config) ||
        !platform_arena_reserve(&present_arena, present_memory_required()) ||
        !present_init(&present, &present_arena, &world)) {
        std::printf("sandbox: sim/presentation init failed\n");
        platform_arena_release(&present_arena);
        platform_arena_release(&world_arena);
        renderer_destroy_material(rnd, material, 0);
        asset_registry_shutdown(&asset_registry, 0);
        renderer_destroy_mesh(rnd, mesh, 0);
        renderer_destroy(rnd);
        platform_arena_release(&asset_io_arena);
        platform_arena_release(&asset_global_arena);
        platform_arena_release(&asset_level_arena);
        platform_arena_release(&asset_persistent_arena);
        platform_window_close(win);
        return 1;
    }
    PlatformFixedStep fixed_step{};
    platform_fixed_step_init(&fixed_step);

    // M3.3: deterministic per-tick orbit commands (pure integer math — fix_sin/cos
    // tables; no RNG, no floats). Each unit circles the origin at its own phase.
    // The caller invokes this once for each owed tick. That keeps the loop ready for
    // a future per-tick input/net command source without changing same-tick semantics.
    auto orbit_commands = [](const SimWorld* world, SimCommand* out, uint32_t* out_count) {
        uint32_t n = 0;
        const int32_t step = (int32_t)((world->tick * 4u) % 360u);
        const mm::fix deg_to_rad = mm::fix_div(FIX_TWO_PI, mm::fix_from_int(360));
        for (uint32_t slot = 0; slot < SIM_MAX_UNITS; ++slot) {
            const mm::fix deg = mm::fix_from_int((int32_t)((slot * 13u + step) % 360u));
            const mm::fix rad = mm::fix_mul(deg, deg_to_rad);
            out[n].kind = SIM_COMMAND_SET_VELOCITY;
            out[n].unit_index = (uint16_t)slot;
            out[n].value_x = mm::fix_mul(mm::fix_sin(rad), mm::fix_from_int(30));
            out[n].value_y = mm::fix_mul(mm::fix_cos(rad), mm::fix_from_int(30));
            ++n;
        }
        *out_count = n;
    };

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
    bool simulation_failed = false;

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

        // Platform owns cadence. Generate a fresh command buffer for every owed tick,
        // advance, capture the completed state, then consume time. A failed tick keeps
        // the debt intact and terminates the app instead of silently dropping time.
        platform_fixed_step_add_frame_delta(&fixed_step, dt);
        while (platform_fixed_step_tick_owed(&fixed_step)) {
            SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
            uint32_t command_count = 0u;
            orbit_commands(&world, commands, &command_count);
            SimCommandBuffer command_buffer{commands, command_count};
            if (!sim_tick(&world, &command_buffer) ||
                !present_capture(&present, &world) ||
                !platform_fixed_step_consume_tick(&fixed_step)) {
                std::printf("sandbox: simulation tick/capture failed at tick %llu\n",
                            (unsigned long long)world.tick);
                simulation_failed = true;
                break;
            }
        }
        if (simulation_failed) break;

        DrawItem unit_items[SIM_MAX_UNITS]{};
        uint32_t unit_count = 0;
        if (rnd)
            unit_count = present_build_draw_items(&present,
                                                  platform_fixed_step_alpha(&fixed_step),
                                                  mesh, material,
                                                  unit_items, SIM_MAX_UNITS);

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
            std::printf("  frame %ld  dt=%.2fms  size=%dx%d  focus=%d  min=%d  cam=(%.2f, %.2f, %.2f)  sim_tick=%llu units=%u\n",
                        frame, dt * 1000.0, in.fb_width, in.fb_height,
                        (int)in.window_focused, (int)in.window_minimized, cam_yaw, cam_pitch, cam_dist,
                        (unsigned long long)world.tick, unit_count);
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
                renderer_submit(rnd, unit_items, unit_count);
                dbg_aabb(rnd, mm::vec3_make(-20.0f, 0.0f, -20.0f), mm::vec3_make(20.0f, 1.0f, 20.0f), 0xff00ffffu);
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
    asset_registry_shutdown(&asset_registry, 0);
    renderer_destroy_mesh(rnd, mesh, 0);
    renderer_destroy(rnd);
    platform_arena_release(&asset_io_arena);
    platform_arena_release(&asset_global_arena);
    platform_arena_release(&asset_level_arena);
    platform_arena_release(&asset_persistent_arena);
    platform_arena_release(&present_arena);
    platform_arena_release(&world_arena);
    platform_window_close(win);
    std::printf("sandbox: clean exit after %ld frames (%.2fs)\n", frame, elapsed);
    return simulation_failed ? 1 : 0;
}
