#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>

struct OwnedTestDirectory {
    char path[128];
    wchar_t wide_path[128];
    HANDLE custody;
};

inline bool test_create_owned_directory(const char* label, OwnedTestDirectory* out) {
    if (!label || !out) return false;
    static volatile LONG sequence = 0;
    for (uint32_t attempt = 0u; attempt < 64u; ++attempt) {
        OwnedTestDirectory candidate{};
        const unsigned long id = (unsigned long)InterlockedIncrement(&sequence);
        const int chars = std::snprintf(
            candidate.path, sizeof(candidate.path), "moba_owned_%s_%lu_%lu", label,
            (unsigned long)GetCurrentProcessId(), id);
        if (chars <= 0 || (size_t)chars >= sizeof(candidate.path) ||
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, candidate.path, -1,
                                candidate.wide_path,
                                (int)(sizeof(candidate.wide_path) /
                                      sizeof(candidate.wide_path[0]))) == 0) return false;
        if (!CreateDirectoryW(candidate.wide_path, nullptr)) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
            return false;
        }

        candidate.custody = CreateFileW(
            candidate.wide_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        FILE_ATTRIBUTE_TAG_INFO info{};
        const bool owned = candidate.custody != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandleEx(candidate.custody, FileAttributeTagInfo,
                                         &info, sizeof(info)) != 0 &&
            (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
            (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u;
        if (!owned) {
            if (candidate.custody != INVALID_HANDLE_VALUE) CloseHandle(candidate.custody);
            RemoveDirectoryW(candidate.wide_path);
            return false;
        }
        *out = candidate;
        return true;
    }
    return false;
}

inline bool test_release_owned_directory(OwnedTestDirectory* owned) {
    if (!owned || owned->custody == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO info{};
    const bool still_owned = GetFileInformationByHandleEx(
        owned->custody, FileAttributeTagInfo, &info, sizeof(info)) != 0 &&
        (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
        (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u;
    CloseHandle(owned->custody);
    owned->custody = INVALID_HANDLE_VALUE;
    return still_owned && RemoveDirectoryW(owned->wide_path) != 0;
}
