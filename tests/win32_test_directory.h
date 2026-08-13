#pragma once

#include <windows.h>
#include <winternl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

struct OwnedTestDirectory {
    char scope_path[128];
    wchar_t wide_scope_path[128];
    char path[160];
    wchar_t wide_path[160];
    HANDLE scope_custody;
    HANDLE directory_custody;
    FILE_ID_INFO directory_id;
};

typedef NTSTATUS (NTAPI* TestNtCreateFileFn)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
    ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

inline bool test_mark_directory_for_delete(HANDLE directory) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(directory, FileDispositionInfo,
                                      &disposition, sizeof(disposition)) != 0;
}

inline bool test_directory_identity(HANDLE directory, FILE_ID_INFO* out_id) {
    if (!out_id) return false;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    return GetFileInformationByHandleEx(directory, FileAttributeTagInfo,
                                        &attributes, sizeof(attributes)) != 0 &&
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u &&
        GetFileInformationByHandleEx(directory, FileIdInfo,
                                     out_id, sizeof(*out_id)) != 0;
}

inline bool test_same_directory_identity(const FILE_ID_INFO& a,
                                         const FILE_ID_INFO& b) {
    return a.VolumeSerialNumber == b.VolumeSerialNumber &&
        std::memcmp(&a.FileId, &b.FileId, sizeof(a.FileId)) == 0;
}

inline bool test_create_directory_relative(HANDLE parent,
                                           const wchar_t* relative_path,
                                           ACCESS_MASK access,
                                           ULONG share_access,
                                           HANDLE* out_directory) {
    if (parent == INVALID_HANDLE_VALUE || !relative_path || !out_directory)
        return false;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    TestNtCreateFileFn nt_create_file = ntdll
        ? (TestNtCreateFileFn)(void*)GetProcAddress(ntdll, "NtCreateFile")
        : nullptr;
    if (!nt_create_file) return false;

    const size_t name_chars = std::wcslen(relative_path);
    if (name_chars == 0u || name_chars > USHRT_MAX / sizeof(wchar_t)) {
        return false;
    }
    UNICODE_STRING name{};
    name.Buffer = (PWSTR)relative_path;
    name.Length = (USHORT)(name_chars * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &name,
                               OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE,
                               parent, nullptr);
    IO_STATUS_BLOCK status_block{};
    HANDLE created = INVALID_HANDLE_VALUE;
    const NTSTATUS status = nt_create_file(
        &created, access, &attributes, &status_block, nullptr,
        FILE_ATTRIBUTE_NORMAL, share_access,
        FILE_CREATE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
        FILE_OPEN_REPARSE_POINT, nullptr, 0u);
    if (status < 0 || created == INVALID_HANDLE_VALUE) return false;
    *out_directory = created;
    return true;
}

inline bool test_create_scope_directory(const wchar_t* relative_path,
                                        HANDLE* out_directory) {
    if (!relative_path || !out_directory) return false;
    HANDLE parent = CreateFileW(
        L".", FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (parent == INVALID_HANDLE_VALUE) return false;
    const bool created = test_create_directory_relative(
        parent, relative_path,
        DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ, out_directory);
    CloseHandle(parent);
    return created;
}

inline bool test_create_owned_directory(const char* label, OwnedTestDirectory* out) {
    if (!label || !out) return false;
    static volatile LONG sequence = 0;
    for (uint32_t attempt = 0u; attempt < 64u; ++attempt) {
        OwnedTestDirectory candidate{};
        candidate.scope_custody = INVALID_HANDLE_VALUE;
        candidate.directory_custody = INVALID_HANDLE_VALUE;
        const unsigned long id = (unsigned long)InterlockedIncrement(&sequence);
        const int scope_chars = std::snprintf(
            candidate.scope_path, sizeof(candidate.scope_path),
            "moba_owned_%s_%lu_%lu", label,
            (unsigned long)GetCurrentProcessId(), id);
        if (scope_chars <= 0 ||
            (size_t)scope_chars >= sizeof(candidate.scope_path) ||
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                candidate.scope_path, -1,
                                candidate.wide_scope_path,
                                (int)(sizeof(candidate.wide_scope_path) /
                                      sizeof(candidate.wide_scope_path[0]))) == 0)
            return false;

        if (!test_create_scope_directory(candidate.wide_scope_path,
                                         &candidate.scope_custody)) continue;

        const int path_chars = std::snprintf(
            candidate.path, sizeof(candidate.path), "%s/work", candidate.scope_path);
        const int wide_path_chars = swprintf_s(
            candidate.wide_path, L"%s\\work", candidate.wide_scope_path);
        if (path_chars <= 0 || (size_t)path_chars >= sizeof(candidate.path) ||
            wide_path_chars <= 0) {
            test_mark_directory_for_delete(candidate.scope_custody);
            CloseHandle(candidate.scope_custody);
            return false;
        }

        const bool child_created = test_create_directory_relative(
            candidate.scope_custody, L"work",
            FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ,
            &candidate.directory_custody);
        FILE_ID_INFO identity{};
        if (!child_created || candidate.directory_custody == INVALID_HANDLE_VALUE ||
            !test_directory_identity(candidate.directory_custody, &identity)) {
            if (candidate.directory_custody != INVALID_HANDLE_VALUE)
                CloseHandle(candidate.directory_custody);
            test_mark_directory_for_delete(candidate.scope_custody);
            CloseHandle(candidate.scope_custody);
            return false;
        }
        candidate.directory_id = identity;
        *out = candidate;
        return true;
    }
    return false;
}

inline bool test_release_owned_directory(OwnedTestDirectory* owned) {
    if (!owned || owned->scope_custody == INVALID_HANDLE_VALUE ||
        owned->directory_custody == INVALID_HANDLE_VALUE) return false;

    FILE_ID_INFO live_identity{};
    const bool custody_matches =
        test_directory_identity(owned->directory_custody, &live_identity) &&
        test_same_directory_identity(live_identity, owned->directory_id);
    CloseHandle(owned->directory_custody);
    owned->directory_custody = INVALID_HANDLE_VALUE;
    if (!custody_matches) {
        CloseHandle(owned->scope_custody);
        owned->scope_custody = INVALID_HANDLE_VALUE;
        return false;
    }

    HANDLE cleanup = CreateFileW(
        owned->wide_path, DELETE | FILE_READ_ATTRIBUTES, 0u, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    FILE_ID_INFO cleanup_identity{};
    const bool exact_inner = cleanup != INVALID_HANDLE_VALUE &&
        test_directory_identity(cleanup, &cleanup_identity) &&
        test_same_directory_identity(cleanup_identity, owned->directory_id);
    const bool inner_disposed = exact_inner && test_mark_directory_for_delete(cleanup);
    if (cleanup != INVALID_HANDLE_VALUE) CloseHandle(cleanup);

    const bool scope_disposed = inner_disposed &&
        test_mark_directory_for_delete(owned->scope_custody);
    CloseHandle(owned->scope_custody);
    owned->scope_custody = INVALID_HANDLE_VALUE;
    return scope_disposed;
}
