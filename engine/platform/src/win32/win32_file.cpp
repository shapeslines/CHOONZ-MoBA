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
    if (!out) return false;
    wchar_t wpath[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;   // missing file is a normal outcome — no log

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 || (uint64_t)li.QuadPart > (uint64_t)(SIZE_MAX / 2)) {
        CloseHandle(h);
        return false;
    }
    size_t size = (size_t)li.QuadPart;

    // 0-byte files still get a valid pointer so out->data is never null on success.
    void* buf = mem_alloc(alloc, size ? size : 1, MEM_DEFAULT_ALIGN);
    if (!buf) { CloseHandle(h); return false; }

    size_t got = 0;
    while (got < size) {
        DWORD chunk = (size - got > 0x40000000u) ? 0x40000000u : (DWORD)(size - got);  // <=1 GiB per ReadFile
        DWORD rd = 0;
        if (!ReadFile(h, (uint8_t*)buf + got, chunk, &rd, nullptr) || rd == 0) break;
        got += rd;
    }
    CloseHandle(h);
    if (got != size) {                       // truncated/failed mid-read
        mem_free(alloc, buf, size ? size : 1);   // no-op for arenas; real free for heap allocators
        platform_log("platform: short read on '%s' (%zu of %zu bytes)\n", path, got, size);
        return false;
    }
    out->data = buf;
    out->size = size;
    return true;
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

bool win32_platform_file_write_with_hook(
    const char* path, const void* data, size_t size,
    Win32FileWriteAfterFlushHook after_flush, void* user) {
    if (!path || (!data && size)) return false;
    wchar_t wpath[1024], wfull[1024], wtmp[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;
    const DWORD full_length = GetFullPathNameW(wpath, 1024, wfull, nullptr);
    if (full_length == 0 || full_length >= 1024 || wcslen(wfull) + 5 > 1024) return false;
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
        platform_log("platform: create '%s.tmp' failed (%lu)\n", path, (unsigned long)GetLastError());
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
                "platform: discard '%s.tmp' failed (%lu)\n", path,
                (unsigned long)GetLastError());
        }
        CloseHandle(h);
        platform_log("platform: write '%s' failed (%lu)\n", path, (unsigned long)failure);
        SetLastError(failure);
        return false;
    }
    CloseHandle(h);
    return true;
}

bool platform_file_write(const char* path, const void* data, size_t size) {
    return win32_platform_file_write_with_hook(path, data, size, nullptr, nullptr);
}
