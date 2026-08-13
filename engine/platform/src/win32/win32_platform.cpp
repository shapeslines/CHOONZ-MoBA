// Win32 backend for the platform seam (ADR-0005, ARCHITECTURE §4.3/4.4).
// Single-threaded; non-blocking PeekMessage pump; basic WM_KEY*/mouse input
// (Raw Input deferred to M3). No GDI background erase (Vulkan owns the surface).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <timeapi.h>    // timeBeginPeriod / timeEndPeriod (winmm)
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include "platform/platform.h"
#include "platform/platform_vulkan.h"

// Vulkan surface creation lives here (HWND never crosses into render, ADR-0005). Only
// compiled when the SDK is present; the headless/CI build stays Vulkan-header-free.
#if defined(MOBA_HAVE_VULKAN)
#  define VK_NO_PROTOTYPES
#  define VK_USE_PLATFORM_WIN32_KHR
#  pragma warning(push)
#  pragma warning(disable: 4255)
#  include <vulkan/vulkan.h>
#  pragma warning(pop)
#endif

// ---- Internal window state (the opaque PlatformWindow) ----
struct PlatformWindow {
    HWND      hwnd;
    HINSTANCE hinst;
    bool      quit;
    bool      minimized;
    bool      focused;
    bool      resized_this_frame;     // set on WM_SIZE, consumed each pump
    int32_t   fb_width, fb_height;

    // Keyboard is persistent state (held keys); the rest below is per-frame and
    // reset at the top of each platform_pump_events().
    uint8_t   key_down[KEY_COUNT];
    uint32_t  key_transitions;
    int32_t   mouse_x, mouse_y;       // latest client position (persistent)
    int32_t   mouse_dx, mouse_dy;     // movement accumulated since last pump
    int32_t   wheel;
    uint8_t   mouse_buttons;
    bool      have_last_mouse;
    int32_t   last_mouse_x, last_mouse_y;
    uint32_t  text[16];
    uint32_t  text_count;
};

static KeyCode vk_to_keycode(int vk) {
    if (vk >= 'A' && vk <= 'Z') return (KeyCode)(KEY_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (KeyCode)(KEY_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (KeyCode)(KEY_F1 + (vk - VK_F1));
    switch (vk) {
        case VK_SPACE:    return KEY_SPACE;
        case VK_ESCAPE:   return KEY_ESCAPE;
        case VK_RETURN:   return KEY_ENTER;
        case VK_TAB:      return KEY_TAB;
        case VK_BACK:     return KEY_BACKSPACE;
        case VK_LEFT:     return KEY_LEFT;
        case VK_RIGHT:    return KEY_RIGHT;
        case VK_UP:       return KEY_UP;
        case VK_DOWN:     return KEY_DOWN;
        case VK_SHIFT: case VK_LSHIFT:   return KEY_LSHIFT;
        case VK_RSHIFT:                  return KEY_RSHIFT;
        case VK_CONTROL: case VK_LCONTROL: return KEY_LCTRL;
        case VK_RCONTROL:                return KEY_RCTRL;
        case VK_MENU: case VK_LMENU:     return KEY_LALT;
        case VK_RMENU:                   return KEY_RALT;
        default:          return KEY_UNKNOWN;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        PlatformWindow* pw = reinterpret_cast<PlatformWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pw));
        pw->hwnd = hwnd;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    PlatformWindow* w = reinterpret_cast<PlatformWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!w) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;                       // never let GDI clear; the renderer owns pixels

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            w->fb_width  = (int32_t)LOWORD(lParam);
            w->fb_height = (int32_t)HIWORD(lParam);
            w->minimized = (wParam == SIZE_MINIMIZED);
            w->resized_this_frame = true;
            return 0;

        case WM_SETFOCUS:
            w->focused = true;
            return 0;

        case WM_KILLFOCUS:                  // flush held keys/buttons: no stuck key on alt-tab
            w->focused = false;
            memset(w->key_down, 0, sizeof(w->key_down));
            w->mouse_buttons = 0;
            return 0;

        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            KeyCode kc = vk_to_keycode((int)wParam);
            bool was_down = ((lParam >> 30) & 1) != 0;
            if (kc != KEY_UNKNOWN) {
                if (!was_down) w->key_transitions++;
                w->key_down[kc] = 1;
            }
            if (msg == WM_KEYDOWN) return 0;
            break;                          // WM_SYSKEYDOWN -> DefWindowProc (Alt+F4, sys menu)
        }

        case WM_SYSKEYUP:
        case WM_KEYUP: {
            KeyCode kc = vk_to_keycode((int)wParam);
            if (kc != KEY_UNKNOWN) { w->key_down[kc] = 0; w->key_transitions++; }
            if (msg == WM_KEYUP) return 0;
            break;
        }

        case WM_CHAR:
            if (w->text_count < 16 && wParam >= 32 && wParam != 127)
                w->text[w->text_count++] = (uint32_t)wParam;
            return 0;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            if (w->have_last_mouse) {
                w->mouse_dx += x - w->last_mouse_x;
                w->mouse_dy += y - w->last_mouse_y;
            }
            w->last_mouse_x = x; w->last_mouse_y = y; w->have_last_mouse = true;
            w->mouse_x = (int32_t)x; w->mouse_y = (int32_t)y;
            return 0;
        }

        case WM_LBUTTONDOWN: w->mouse_buttons = (uint8_t)(w->mouse_buttons | 0x01u); return 0;
        case WM_LBUTTONUP:   w->mouse_buttons = (uint8_t)(w->mouse_buttons & ~0x01u); return 0;
        case WM_RBUTTONDOWN: w->mouse_buttons = (uint8_t)(w->mouse_buttons | 0x02u); return 0;
        case WM_RBUTTONUP:   w->mouse_buttons = (uint8_t)(w->mouse_buttons & ~0x02u); return 0;
        case WM_MBUTTONDOWN: w->mouse_buttons = (uint8_t)(w->mouse_buttons | 0x04u); return 0;
        case WM_MBUTTONUP:   w->mouse_buttons = (uint8_t)(w->mouse_buttons & ~0x04u); return 0;

        case WM_MOUSEWHEEL:
            w->wheel += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Window / loop ----------------------------------------------------------
PlatformWindow* platform_window_open(const PlatformWindowDesc* desc) {
    static const wchar_t* kClassName = L"MobaWindowClass";
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_OWNDC;                 // no CS_HREDRAW/VREDRAW (surface-owned)
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = hinst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) {
            platform_log("platform: RegisterClassExW failed (%lu)\n", (unsigned long)GetLastError());
            return nullptr;
        }
        registered = true;
    }

    PlatformWindow* w = (PlatformWindow*)calloc(1, sizeof(PlatformWindow));
    if (!w) return nullptr;
    w->hinst   = hinst;
    w->focused = true;

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (desc && !desc->resizable) style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);

    int cw = (desc && desc->width  > 0) ? desc->width  : 1280;
    int ch = (desc && desc->height > 0) ? desc->height : 720;
    w->fb_width = cw; w->fb_height = ch;

    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, style, FALSE, 0);

    wchar_t wtitle[256];
    const char* title = (desc && desc->title) ? desc->title : "MOBA";
    if (MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256) == 0) wcscpy_s(wtitle, 256, L"MOBA");

    HWND hwnd = CreateWindowExW(0, kClassName, wtitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hinst, w);
    if (!hwnd) {
        platform_log("platform: CreateWindowExW failed (%lu)\n", (unsigned long)GetLastError());
        free(w);
        return nullptr;
    }

    timeBeginPeriod(1);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    return w;
}

void platform_window_close(PlatformWindow* w) {
    if (!w) return;
    if (w->hwnd) DestroyWindow(w->hwnd);
    timeEndPeriod(1);
    free(w);
}

bool platform_pump_events(PlatformWindow* w, PlatformFrameInput* out) {
    // Reset per-frame accumulators (keyboard down-state persists).
    w->mouse_dx = 0; w->mouse_dy = 0; w->wheel = 0;
    w->key_transitions = 0; w->text_count = 0; w->resized_this_frame = false;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) w->quit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    memcpy(out->keyboard.down, w->key_down, sizeof(out->keyboard.down));
    out->keyboard.transitions = w->key_transitions;
    out->mouse.dx = w->mouse_dx; out->mouse.dy = w->mouse_dy;
    out->mouse.x  = w->mouse_x;  out->mouse.y  = w->mouse_y;
    out->mouse.wheel = w->wheel; out->mouse.buttons = w->mouse_buttons;
    out->window_resized   = w->resized_this_frame;
    out->window_minimized = w->minimized;
    out->window_focused   = w->focused;
    out->fb_width = w->fb_width; out->fb_height = w->fb_height;
    for (uint32_t i = 0; i < w->text_count; ++i) out->text_utf32[i] = w->text[i];
    out->text_count = w->text_count;
    return !w->quit;
}

void platform_window_size(PlatformWindow* w, int32_t* width, int32_t* height) {
    if (width)  *width  = w ? w->fb_width  : 0;
    if (height) *height = w ? w->fb_height : 0;
}

// ---- Clock ------------------------------------------------------------------
static uint64_t qpc_frequency(void) {
    static uint64_t f = 0;
    if (!f) { LARGE_INTEGER li; QueryPerformanceFrequency(&li); f = (uint64_t)li.QuadPart; }
    return f;
}
uint64_t platform_time_ticks(void)     { LARGE_INTEGER li; QueryPerformanceCounter(&li); return (uint64_t)li.QuadPart; }
uint64_t platform_time_frequency(void) { return qpc_frequency(); }
double   platform_time_seconds(uint64_t ticks) { return (double)ticks / (double)qpc_frequency(); }
void     platform_sleep_ms(uint32_t ms) { Sleep(ms); }

// NOTE: the OS page allocator / arenas, file I/O, and logging live in their own
// TUs (win32_mem.cpp, win32_file.cpp, win32_log.cpp — G14) so the headless test
// group never pulls this window TU.

// ---- Vulkan loader (ADR-0004): hand-load vulkan-1.dll, hand back vkGetInstanceProcAddr ----
PlatformVkProc platform_vk_get_loader(void) {
    static HMODULE vklib = nullptr;
    if (!vklib) vklib = LoadLibraryW(L"vulkan-1.dll");
    if (!vklib) {
        platform_log("platform: LoadLibrary(vulkan-1.dll) failed (%lu)\n", (unsigned long)GetLastError());
        return nullptr;
    }
    return (PlatformVkProc)GetProcAddress(vklib, "vkGetInstanceProcAddr");
}

#if defined(MOBA_HAVE_VULKAN)
bool platform_vk_create_surface(PlatformWindow* window, void* instance, unsigned long long* out_surface) {
    if (!window || !instance || !out_surface) return false;
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_loader();
    if (!gipa) return false;
    VkInstance inst = (VkInstance)instance;
    PFN_vkCreateWin32SurfaceKHR create = (PFN_vkCreateWin32SurfaceKHR)gipa(inst, "vkCreateWin32SurfaceKHR");
    if (!create) { platform_log("platform: vkCreateWin32SurfaceKHR not found\n"); return false; }

    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = window->hinst;
    ci.hwnd      = window->hwnd;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult res = create(inst, &ci, nullptr, &surface);
    if (res != VK_SUCCESS) { platform_log("platform: vkCreateWin32SurfaceKHR failed (%d)\n", (int)res); return false; }
    *out_surface = (unsigned long long)(uintptr_t)surface;
    return true;
}
#endif
