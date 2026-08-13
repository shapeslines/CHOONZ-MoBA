// Win32 file I/O (M2.1; ARCHITECTURE §4.1). Split out of win32_platform.cpp (G14)
// so the headless test group links it without the window TU. UTF-8 paths -> wide
// via MB_ERR_INVALID_CHARS; atomic .tmp + MoveFileExW writes; bounded chunked reads.
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#include "core/mem.h"
#include "platform/platform.h"

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

bool platform_file_write(const char* path, const void* data, size_t size) {
    if (!path || (!data && size)) return false;
    wchar_t wpath[1024], wtmp[1024];
    if (!utf8_to_wide(path, wpath, 1024)) return false;
    if (wcslen(wpath) + 5 > 1024) return false;
    wcscpy_s(wtmp, 1024, wpath);
    wcscat_s(wtmp, 1024, L".tmp");

    // The temporary name is predictable. CREATE_NEW is intentional: never overwrite a
    // stale or attacker-precreated .tmp (which could be a link to another file).
    HANDLE h = CreateFileW(wtmp, GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        platform_log("platform: create '%s.tmp' failed (%lu)\n", path, (unsigned long)GetLastError());
        return false;
    }

    size_t put = 0;
    bool ok = true;
    while (ok && put < size) {
        DWORD chunk = (size - put > 0x40000000u) ? 0x40000000u : (DWORD)(size - put);
        DWORD wr = 0;
        ok = WriteFile(h, (const uint8_t*)data + put, chunk, &wr, nullptr) && wr == chunk;
        put += wr;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok) ok = MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        platform_log("platform: write '%s' failed (%lu)\n", path, (unsigned long)GetLastError());
        DeleteFileW(wtmp);
    }
    return ok;
}
