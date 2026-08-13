#pragma once

#include <stddef.h>

// Internal adversarial seam. The production platform_file_write contract remains
// unchanged; tests use this hook to attack the exact post-flush/pre-commit interval.
using Win32FileWriteAfterFlushHook = void (*)(const wchar_t* temporary_path, void* user);

bool win32_platform_file_write_with_hook(
    const char* path, const void* data, size_t size,
    Win32FileWriteAfterFlushHook after_flush, void* user);
