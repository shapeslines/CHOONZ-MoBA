// Win32 file I/O (M2.1; ARCHITECTURE §4.1). Split out of win32_platform.cpp (G14)
// so the headless test group links it without the window TU. UTF-8 paths -> wide
// via MB_ERR_INVALID_CHARS; atomic .tmp + MoveFileExW writes; bounded chunked reads.
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#include "core/mem.h"
#include "platform/platform.h"
#include "win32_file_internal.h"

// UTF-8 path -> wide. Returns false (and logs) if the path doesn't fit/convert.
static bool utf8_to_wide(const char* path, wchar_t* out, int out_count) {
    if (!path || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, out_count) == 0) {
        platform_log("platform: bad path '%s' (%lu)\n", path ? path : "(null)", (unsigned long)GetLastError());
        return false;
    }
    return true;
}

static bool win32_read_open_file(HANDLE file, size_t max_bytes, Allocator alloc,
                                 PlatformFile* out, const char* display_path);

bool platform_file_size(const char* path, size_t* out_size) {
    if (!out_size) return false;
    wchar_t wpath[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) return false;   // missing — no log
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    *out_size = ((size_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return true;
}

bool platform_file_read(const char* path, Allocator alloc, PlatformFile* out) {
    return platform_file_read_bounded(path, SIZE_MAX, alloc, out);
}

bool platform_file_read_bounded(const char* path, size_t max_bytes,
                                Allocator alloc, PlatformFile* out) {
    if (!out || !alloc.fn) return false;
    wchar_t wpath[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;   // missing file is a normal outcome — no log
    const bool success = win32_read_open_file(h, max_bytes, alloc, out, path);
    CloseHandle(h);
    return success;
}

static bool win32_handle_attributes(HANDLE handle, DWORD* out_attributes) {
    if (!out_attributes) return false;
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                      &info, sizeof(info))) return false;
    *out_attributes = info.FileAttributes;
    return true;
}

static bool win32_final_path(HANDLE handle, wchar_t* out, DWORD capacity,
                             size_t* out_length) {
    if (!out || !out_length || capacity == 0u) return false;
    const DWORD length = GetFinalPathNameByHandleW(
        handle, out, capacity, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0u || length >= capacity) return false;
    *out_length = (size_t)length;
    return true;
}

static bool win32_path_is_descendant(const wchar_t* root, size_t root_length,
                                     const wchar_t* child, size_t child_length) {
    while (root_length != 0u &&
           (root[root_length - 1u] == L'\\' || root[root_length - 1u] == L'/'))
        --root_length;
    if (root_length == 0u || child_length <= root_length ||
        _wcsnicmp(root, child, root_length) != 0) return false;
    return child[root_length] == L'\\' || child[root_length] == L'/';
}

static bool win32_read_open_file(HANDLE file, size_t max_bytes, Allocator alloc,
                                 PlatformFile* out, const char* display_path) {
    LARGE_INTEGER length{};
    if (!out || !alloc.fn || !GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
        (uint64_t)length.QuadPart > (uint64_t)SIZE_MAX ||
        (uint64_t)length.QuadPart > (uint64_t)max_bytes) return false;
    const size_t size = (size_t)length.QuadPart;
    void* bytes = mem_alloc(alloc, size != 0u ? size : 1u, MEM_DEFAULT_ALIGN);
    if (!bytes) return false;

    size_t received = 0u;
    while (received < size) {
        const DWORD chunk = size - received > 0x40000000u
            ? 0x40000000u : (DWORD)(size - received);
        DWORD read = 0u;
        if (!ReadFile(file, (uint8_t*)bytes + received, chunk, &read, nullptr) ||
            read == 0u) break;
        received += read;
    }
    if (received != size) {
        mem_free(alloc, bytes, size != 0u ? size : 1u);
        platform_log("platform: short rooted read on '%s' (%zu of %zu bytes)\n",
                     display_path ? display_path : "(null)", received, size);
        return false;
    }
    PlatformFile result{bytes, size};
    *out = result;
    return true;
}

bool platform_file_read_rooted(const char* root, const char* relative_path,
                               size_t max_bytes, Allocator alloc,
                               PlatformFile* out) {
    if (!root || !relative_path || !out || !alloc.fn || relative_path[0] == '\0' ||
        relative_path[0] == '/' || relative_path[0] == '\\') return false;

    wchar_t wide_root[1024];
    wchar_t wide_relative[1024];
    wchar_t full_root[1024];
    if (!utf8_to_wide(root, wide_root, 1024) ||
        !utf8_to_wide(relative_path, wide_relative, 1024)) return false;
    const DWORD full_root_length = GetFullPathNameW(wide_root, 1024, full_root, nullptr);
    if (full_root_length == 0u || full_root_length >= 1024u) return false;

    HANDLE root_handle = CreateFileW(
        full_root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (root_handle == INVALID_HANDLE_VALUE) return false;

    HANDLE components[512];
    uint32_t component_count = 0u;
    bool success = false;
    DWORD root_attributes = 0u;
    wchar_t final_root[1024];
    size_t final_root_length = 0u;
    if (!win32_handle_attributes(root_handle, &root_attributes) ||
        (root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ||
        !win32_final_path(root_handle, final_root, 1024u, &final_root_length)) {
        CloseHandle(root_handle);
        return false;
    }

    wchar_t current[2048];
    const size_t root_chars = wcslen(full_root);
    if (root_chars + 2u > sizeof(current) / sizeof(current[0])) {
        CloseHandle(root_handle);
        return false;
    }
    memcpy(current, full_root, (root_chars + 1u) * sizeof(wchar_t));
    size_t current_length = root_chars;
    if (current_length != 0u && current[current_length - 1u] != L'\\' &&
        current[current_length - 1u] != L'/') current[current_length++] = L'\\';

    size_t read = 0u;
    for (;;) {
        const size_t segment_begin = read;
        while (wide_relative[read] != L'\0' && wide_relative[read] != L'/') {
            if (wide_relative[read] == L'\\' || wide_relative[read] == L':') goto cleanup;
            ++read;
        }
        const size_t segment_length = read - segment_begin;
        if (segment_length == 0u ||
            (segment_length == 1u && wide_relative[segment_begin] == L'.') ||
            (segment_length == 2u && wide_relative[segment_begin] == L'.' &&
             wide_relative[segment_begin + 1u] == L'.') ||
            component_count >= sizeof(components) / sizeof(components[0]) ||
            segment_length + current_length + 1u >
                sizeof(current) / sizeof(current[0])) goto cleanup;

        memcpy(current + current_length, wide_relative + segment_begin,
               segment_length * sizeof(wchar_t));
        current_length += segment_length;
        current[current_length] = L'\0';
        const bool final_component = wide_relative[read] == L'\0';
        const DWORD access = final_component ? GENERIC_READ : FILE_READ_ATTRIBUTES;
        const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
                            FILE_FLAG_OPEN_REPARSE_POINT |
                            (final_component ? FILE_FLAG_SEQUENTIAL_SCAN : 0u);
        HANDLE component = CreateFileW(current, access, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, flags, nullptr);
        if (component == INVALID_HANDLE_VALUE) goto cleanup;
        components[component_count++] = component;

        DWORD attributes = 0u;
        if (!win32_handle_attributes(component, &attributes) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ||
            (!final_component && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) ||
            (final_component && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u))
            goto cleanup;

        if (final_component) {
            wchar_t final_file[1024];
            size_t final_file_length = 0u;
            BY_HANDLE_FILE_INFORMATION file_info{};
            if (!win32_final_path(component, final_file, 1024u, &final_file_length) ||
                !win32_path_is_descendant(final_root, final_root_length,
                                          final_file, final_file_length) ||
                !GetFileInformationByHandle(component, &file_info) ||
                file_info.nNumberOfLinks != 1u) goto cleanup;
            success = win32_read_open_file(component, max_bytes, alloc, out,
                                           relative_path);
            goto cleanup;
        }
        current[current_length++] = L'\\';
        ++read;
        if (wide_relative[read] == L'\0') goto cleanup;
    }

cleanup:
    while (component_count != 0u) CloseHandle(components[--component_count]);
    CloseHandle(root_handle);
    return success;
}

static bool win32_rename_open_file(HANDLE file, const wchar_t* destination) {
    const size_t name_chars = wcslen(destination);
    if (name_chars >= 1024) return false;
    const size_t name_bytes = name_chars * sizeof(wchar_t);

    constexpr size_t k_storage_size =
        offsetof(FILE_RENAME_INFO, FileName) + 1024 * sizeof(wchar_t);
    alignas(FILE_RENAME_INFO) uint8_t storage[k_storage_size];
    ZeroMemory(storage, sizeof(storage));
    FILE_RENAME_INFO* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage);
    rename->ReplaceIfExists = TRUE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name_bytes);
    CopyMemory(rename->FileName, destination, name_bytes);
    const DWORD info_size = static_cast<DWORD>(
        offsetof(FILE_RENAME_INFO, FileName) + name_bytes);
    return SetFileInformationByHandle(file, FileRenameInfo, rename, info_size) != 0;
}

static bool win32_discard_open_file(HANDLE file) {
    struct FileDispositionInfoExData {
        DWORD flags;
    };
    constexpr auto k_file_disposition_info_ex =
        static_cast<FILE_INFO_BY_HANDLE_CLASS>(21);
    constexpr DWORD k_file_disposition_delete = 0x1u;
    constexpr DWORD k_file_disposition_ignore_readonly = 0x10u;
    FileDispositionInfoExData disposition{
        k_file_disposition_delete | k_file_disposition_ignore_readonly};
    return SetFileInformationByHandle(
               file, k_file_disposition_info_ex, &disposition,
               static_cast<DWORD>(sizeof(disposition))) != 0;
}

static bool win32_file_write_full_with_hook(
    const char* display_path, const wchar_t* wfull, const void* data, size_t size,
    Win32FileWriteAfterFlushHook after_flush, void* user) {
    if (!display_path || !wfull || (!data && size) || wcslen(wfull) + 5u > 1024u)
        return false;
    wchar_t wtmp[1024];
    wcscpy_s(wtmp, 1024, wfull);
    wcscat_s(wtmp, 1024, L".tmp");

    // The temporary name is predictable. CREATE_NEW is intentional: never overwrite a
    // stale or attacker-precreated .tmp (which could be a link to another file).
    // DELETE access and the exclusive share keep this exact object bound through
    // handle-based rename or handle-based failure cleanup.
    HANDLE h = CreateFileW(
        wtmp, GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        platform_log("platform: create '%s.tmp' failed (%lu)\n", display_path,
                     (unsigned long)GetLastError());
        return false;
    }

    size_t put = 0;
    bool ok = true;
    DWORD failure = ERROR_SUCCESS;
    while (ok && put < size) {
        DWORD chunk = (size - put > 0x40000000u) ? 0x40000000u : (DWORD)(size - put);
        DWORD wr = 0;
        const BOOL wrote = WriteFile(h, (const uint8_t*)data + put, chunk, &wr, nullptr);
        ok = wrote != 0 && wr == chunk;
        if (!ok) failure = wrote ? ERROR_WRITE_FAULT : GetLastError();
        put += wr;
    }
    if (ok && !FlushFileBuffers(h)) {
        failure = GetLastError();
        ok = false;
    }
    if (ok && after_flush) after_flush(wtmp, user);
    if (ok && !win32_rename_open_file(h, wfull)) {
        failure = GetLastError();
        ok = false;
    }
    if (!ok) {
        if (failure == ERROR_SUCCESS) failure = ERROR_WRITE_FAULT;
        if (!win32_discard_open_file(h)) {
            platform_log(
                "platform: discard '%s.tmp' failed (%lu)\n", display_path,
                (unsigned long)GetLastError());
        }
        CloseHandle(h);
        platform_log("platform: write '%s' failed (%lu)\n", display_path,
                     (unsigned long)failure);
        SetLastError(failure);
        return false;
    }
    CloseHandle(h);
    return true;
}

bool win32_platform_file_write_with_hook(
    const char* path, const void* data, size_t size,
    Win32FileWriteAfterFlushHook after_flush, void* user) {
    if (!path || (!data && size)) return false;
    wchar_t wpath[1024], wfull[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;
    const DWORD full_length = GetFullPathNameW(wpath, 1024, wfull, nullptr);
    if (full_length == 0u || full_length >= 1024u) return false;
    return win32_file_write_full_with_hook(path, wfull, data, size,
                                           after_flush, user);
}

bool platform_file_write(const char* path, const void* data, size_t size) {
    return win32_platform_file_write_with_hook(path, data, size, nullptr, nullptr);
}

bool platform_file_write_rooted(const char* root, const char* relative_path,
                                const void* data, size_t size) {
    if (!root || !relative_path || (!data && size) || relative_path[0] == '\0' ||
        relative_path[0] == '/' || relative_path[0] == '\\') return false;

    wchar_t wide_root[1024];
    wchar_t wide_relative[1024];
    wchar_t full_root[1024];
    if (!utf8_to_wide(root, wide_root, 1024) ||
        !utf8_to_wide(relative_path, wide_relative, 1024)) return false;
    const DWORD full_root_length = GetFullPathNameW(wide_root, 1024, full_root, nullptr);
    if (full_root_length == 0u || full_root_length >= 1024u) return false;

    HANDLE root_handle = CreateFileW(
        full_root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (root_handle == INVALID_HANDLE_VALUE) return false;

    HANDLE components[512];
    uint32_t component_count = 0u;
    bool success = false;
    DWORD root_attributes = 0u;
    wchar_t final_root[1024];
    size_t final_root_length = 0u;
    wchar_t current[1024]{};
    const size_t root_chars = wcslen(full_root);
    size_t current_length = 0u;
    size_t cursor = 0u;
    if (!win32_handle_attributes(root_handle, &root_attributes) ||
        (root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ||
        !win32_final_path(root_handle, final_root, 1024u, &final_root_length))
        goto cleanup;

    if (root_chars + 2u > sizeof(current) / sizeof(current[0])) goto cleanup;
    memcpy(current, full_root, (root_chars + 1u) * sizeof(wchar_t));
    current_length = root_chars;
    if (current_length != 0u && current[current_length - 1u] != L'\\' &&
        current[current_length - 1u] != L'/') current[current_length++] = L'\\';

    for (;;) {
        const size_t segment_begin = cursor;
        while (wide_relative[cursor] != L'\0' && wide_relative[cursor] != L'/') {
            if (wide_relative[cursor] == L'\\' || wide_relative[cursor] == L':')
                goto cleanup;
            ++cursor;
        }
        const size_t segment_length = cursor - segment_begin;
        if (segment_length == 0u ||
            (segment_length == 1u && wide_relative[segment_begin] == L'.') ||
            (segment_length == 2u && wide_relative[segment_begin] == L'.' &&
             wide_relative[segment_begin + 1u] == L'.') ||
            segment_length + current_length + 1u >
                sizeof(current) / sizeof(current[0])) goto cleanup;

        memcpy(current + current_length, wide_relative + segment_begin,
               segment_length * sizeof(wchar_t));
        current_length += segment_length;
        current[current_length] = L'\0';
        const bool final_component = wide_relative[cursor] == L'\0';
        if (final_component) {
            success = win32_file_write_full_with_hook(
                relative_path, current, data, size, nullptr, nullptr);
            goto cleanup;
        }

        if (component_count >= sizeof(components) / sizeof(components[0]))
            goto cleanup;
        HANDLE component = CreateFileW(
            current, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (component == INVALID_HANDLE_VALUE) goto cleanup;
        components[component_count++] = component;

        DWORD attributes = 0u;
        wchar_t final_component_path[1024];
        size_t final_component_length = 0u;
        if (!win32_handle_attributes(component, &attributes) ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ||
            !win32_final_path(component, final_component_path, 1024u,
                              &final_component_length) ||
            !win32_path_is_descendant(final_root, final_root_length,
                                      final_component_path,
                                      final_component_length)) goto cleanup;

        current[current_length++] = L'\\';
        ++cursor;
        if (wide_relative[cursor] == L'\0') goto cleanup;
    }

cleanup:
    while (component_count != 0u) CloseHandle(components[--component_count]);
    CloseHandle(root_handle);
    return success;
}
