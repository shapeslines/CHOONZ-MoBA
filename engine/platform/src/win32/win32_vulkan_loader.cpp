#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cwchar>

#include "platform/platform.h"
#include "platform/platform_vulkan.h"
#include "win32/win32_vulkan_loader.h"

static bool is_drive_absolute_path(const wchar_t* path) {
    if (!path || path[0] == L'\0' || path[1] != L':') return false;
    const wchar_t drive = path[0];
    const bool ascii_letter = (drive >= L'A' && drive <= L'Z') ||
                              (drive >= L'a' && drive <= L'z');
    return ascii_letter && (path[2] == L'\\' || path[2] == L'/');
}

bool win32_vulkan_sdk_loader_path(const wchar_t* sdk_root,
                                  wchar_t* out_path, size_t out_count) {
    if (!out_path || out_count == 0 || !is_drive_absolute_path(sdk_root)) return false;

    wchar_t canonical_root[1024]{};
    const DWORD root_chars = GetFullPathNameW(sdk_root, ARRAYSIZE(canonical_root),
                                               canonical_root, nullptr);
    if (root_chars == 0 || root_chars >= ARRAYSIZE(canonical_root) ||
        !is_drive_absolute_path(canonical_root)) return false;

    const DWORD root_attributes = GetFileAttributesW(canonical_root);
    if (root_attributes == INVALID_FILE_ATTRIBUTES ||
        (root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;

    wchar_t candidate[1024]{};
    const int written = swprintf_s(candidate, ARRAYSIZE(candidate),
                                   L"%ls\\Bin\\vulkan-1.dll", canonical_root);
    if (written < 0 || (size_t)written + 1u > out_count) return false;

    wcscpy_s(out_path, out_count, candidate);
    return true;
}

static HMODULE load_vulkan_library(void) {
    // Never use the ambient DLL search order. The installed runtime wins; an SDK
    // fallback must be an explicit canonical absolute path with restricted dependency search.
    HMODULE vklib = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (vklib) return vklib;

    wchar_t sdk_root[1024]{};
    const DWORD sdk_chars = GetEnvironmentVariableW(L"VULKAN_SDK", sdk_root,
                                                     ARRAYSIZE(sdk_root));
    if (sdk_chars == 0 || sdk_chars >= ARRAYSIZE(sdk_root)) return nullptr;

    wchar_t sdk_loader[1024]{};
    if (!win32_vulkan_sdk_loader_path(sdk_root, sdk_loader, ARRAYSIZE(sdk_loader)))
        return nullptr;
    return LoadLibraryExW(sdk_loader, nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

PlatformVkProc platform_vk_get_loader(void) {
    static HMODULE vklib = nullptr;
    if (!vklib) vklib = load_vulkan_library();
    if (!vklib) {
        platform_log("platform: restricted Vulkan loader lookup failed (%lu)\n",
                     (unsigned long)GetLastError());
        return nullptr;
    }
    return (PlatformVkProc)GetProcAddress(vklib, "vkGetInstanceProcAddr");
}
