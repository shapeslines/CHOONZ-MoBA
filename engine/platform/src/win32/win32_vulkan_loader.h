#pragma once
#include <stddef.h>

// Internal, testable path boundary for the explicit VULKAN_SDK fallback. Accepts
// only an existing drive-absolute SDK root and writes a canonical full loader path.
// `out_path` is untouched on failure.
bool win32_vulkan_sdk_loader_path(const wchar_t* sdk_root,
                                  wchar_t* out_path, size_t out_count);
