// M2.1 platform file I/O tests — read-into-allocator + atomic whole-file write
// (ARCHITECTURE §4.1). Runs headlessly; files land in the CTest working dir and are
// removed at the end of each case (remove() — the seam has no delete on purpose).
#include "test.h"
#include "core/mem.h"
#include "core/sim_config.h"
#include "platform/platform.h"
#include "platform/platform_fixed_step.h"
#include "win32_file_internal.h"
#include "win32_vulkan_loader.h"
#include <windows.h>
#include "win32_test_directory.h"
#include <winioctl.h>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <io.h>         // _chmod (force a rename failure via a read-only destination)
#include <sys/stat.h>   // _S_IREAD / _S_IWRITE

static uint32_t consume_all_owed_ticks(PlatformFixedStep* step) {
    uint32_t count = 0u;
    while (platform_fixed_step_tick_owed(step)) {
        CHECK(platform_fixed_step_consume_tick(step));
        ++count;
    }
    return count;
}

TEST(platform, fixed_step_groups_render_deltas_into_the_same_ticks) {
    PlatformFixedStep at_60{}, at_30{}, mixed{};
    platform_fixed_step_init(&at_60);
    platform_fixed_step_init(&at_30);
    platform_fixed_step_init(&mixed);

    uint32_t ticks_60 = 0u;
    uint32_t ticks_30 = 0u;
    uint32_t ticks_mixed = 0u;
    for (uint32_t frame = 0u; frame < 60u; ++frame) {
        platform_fixed_step_add_frame_delta(&at_60, SIM_DT_SECONDS * 0.5);
        ticks_60 += consume_all_owed_ticks(&at_60);
    }
    for (uint32_t frame = 0u; frame < 30u; ++frame) {
        platform_fixed_step_add_frame_delta(&at_30, SIM_DT_SECONDS);
        ticks_30 += consume_all_owed_ticks(&at_30);
    }
    for (uint32_t frame = 0u; frame < 30u; ++frame) {
        const double delta = (frame & 1u) ? SIM_DT_SECONDS * 1.5 : SIM_DT_SECONDS * 0.5;
        platform_fixed_step_add_frame_delta(&mixed, delta);
        ticks_mixed += consume_all_owed_ticks(&mixed);
    }

    CHECK(ticks_60 == 30u);
    CHECK(ticks_30 == 30u);
    CHECK(ticks_mixed == 30u);
    CHECK_APPROX(platform_fixed_step_alpha(&at_60), 0.0, 1.0e-9);
    CHECK_APPROX(platform_fixed_step_alpha(&at_30), 0.0, 1.0e-9);
    CHECK_APPROX(platform_fixed_step_alpha(&mixed), 0.0, 1.0e-9);
}

TEST(platform, fixed_step_clamps_and_consumes_only_when_owed) {
    PlatformFixedStep step{};
    platform_fixed_step_init(&step);
    CHECK(!platform_fixed_step_tick_owed(&step));
    CHECK(!platform_fixed_step_consume_tick(&step));
    CHECK(step.accumulator_seconds == 0.0);

    platform_fixed_step_add_frame_delta(&step, -1.0);
    CHECK(step.accumulator_seconds == 0.0);
    platform_fixed_step_add_frame_delta(&step, 10.0);
    CHECK(step.accumulator_seconds == SIM_MAX_CATCHUP_S);
    CHECK(consume_all_owed_ticks(&step) == 7u);
    CHECK_APPROX(platform_fixed_step_alpha(&step), 0.5, 1.0e-9);

    const double before = step.accumulator_seconds;
    CHECK(!platform_fixed_step_consume_tick(&step));
    CHECK(step.accumulator_seconds == before);
}

static Allocator test_arena_alloc(Arena* a, uint8_t* buf, size_t n) {
    arena_init_fixed(a, buf, n);
    return arena_allocator(a);
}

struct TestMountPointBuffer {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    USHORT substitute_name_offset;
    USHORT substitute_name_length;
    USHORT print_name_offset;
    USHORT print_name_length;
    wchar_t path_buffer[1024];
};

static bool test_create_junction(const wchar_t* link_path, const wchar_t* target_path) {
    wchar_t target_full[MAX_PATH];
    const DWORD target_length = GetFullPathNameW(target_path, MAX_PATH, target_full, nullptr);
    if (target_length == 0u || target_length >= MAX_PATH ||
        !CreateDirectoryW(link_path, nullptr)) return false;

    HANDLE link = CreateFileW(
        link_path, GENERIC_WRITE, 0u, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (link == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(link_path);
        return false;
    }

    TestMountPointBuffer buffer{};
    buffer.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    const wchar_t prefix[] = L"\\??\\";
    const size_t prefix_length = (sizeof(prefix) / sizeof(prefix[0])) - 1u;
    const size_t substitute_chars = prefix_length + target_length;
    const size_t print_chars = target_length;
    if (substitute_chars + 1u + print_chars + 1u >
        sizeof(buffer.path_buffer) / sizeof(buffer.path_buffer[0])) {
        CloseHandle(link);
        RemoveDirectoryW(link_path);
        return false;
    }
    memcpy(buffer.path_buffer, prefix, prefix_length * sizeof(wchar_t));
    memcpy(buffer.path_buffer + prefix_length, target_full,
           target_length * sizeof(wchar_t));
    buffer.path_buffer[substitute_chars] = L'\0';
    memcpy(buffer.path_buffer + substitute_chars + 1u, target_full,
           print_chars * sizeof(wchar_t));
    buffer.path_buffer[substitute_chars + 1u + print_chars] = L'\0';
    buffer.substitute_name_offset = 0u;
    buffer.substitute_name_length = (USHORT)(substitute_chars * sizeof(wchar_t));
    buffer.print_name_offset = (USHORT)((substitute_chars + 1u) * sizeof(wchar_t));
    buffer.print_name_length = (USHORT)(print_chars * sizeof(wchar_t));
    const size_t path_bytes = (substitute_chars + 1u + print_chars + 1u) *
                              sizeof(wchar_t);
    buffer.reparse_data_length = (USHORT)(8u + path_bytes);
    const DWORD input_bytes = (DWORD)(8u + buffer.reparse_data_length);
    DWORD returned = 0u;
    const bool ok = DeviceIoControl(link, FSCTL_SET_REPARSE_POINT, &buffer,
                                    input_bytes, nullptr, 0u, &returned, nullptr) != 0;
    CloseHandle(link);
    if (!ok) RemoveDirectoryW(link_path);
    return ok;
}

TEST(platform, rooted_file_read_binds_components_and_rejects_junction_escape) {
    OwnedTestDirectory root{};
    OwnedTestDirectory outside{};
    root.scope_custody = INVALID_HANDLE_VALUE;
    root.directory_custody = INVALID_HANDLE_VALUE;
    outside.scope_custody = INVALID_HANDLE_VALUE;
    outside.directory_custody = INVALID_HANDLE_VALUE;
    const bool root_owned = test_create_owned_directory("root", &root);
    CHECK(root_owned);
    if (!root_owned) return;
    const bool outside_owned = test_create_owned_directory("outside", &outside);
    CHECK(outside_owned);
    if (!outside_owned) {
        CHECK(test_release_owned_directory(&root));
        return;
    }

    char inside_path[256];
    char outside_path[256];
    wchar_t junction[256];
    wchar_t hard_link[256];
    wchar_t outside_file_wide[256];
    const int inside_chars = std::snprintf(
        inside_path, sizeof(inside_path), "%s/inside.bin", root.path);
    const int outside_chars = std::snprintf(
        outside_path, sizeof(outside_path), "%s/payload.bin", outside.path);
    const int junction_chars = swprintf_s(junction, L"%s\\escape", root.wide_path);
    const int hard_link_chars = swprintf_s(
        hard_link, L"%s\\outside-hardlink.bin", root.wide_path);
    const int outside_wide_chars = swprintf_s(
        outside_file_wide, L"%s\\payload.bin", outside.wide_path);
    const bool paths_ready = inside_chars > 0 &&
        (size_t)inside_chars < sizeof(inside_path) && outside_chars > 0 &&
        (size_t)outside_chars < sizeof(outside_path) && junction_chars > 0 &&
        hard_link_chars > 0 && outside_wide_chars > 0;
    CHECK(paths_ready);
    if (!paths_ready) {
        CHECK(test_release_owned_directory(&root));
        CHECK(test_release_owned_directory(&outside));
        return;
    }
    const char* inside = "inside-root";
    const char* payload = "outside-sentinel";
    const bool inside_written = platform_file_write(inside_path, inside, std::strlen(inside));
    const bool outside_written = platform_file_write(outside_path, payload, std::strlen(payload));
    CHECK(inside_written && outside_written);
    if (!inside_written || !outside_written) {
        if (inside_written) CHECK(DeleteFileA(inside_path) != 0);
        if (outside_written) CHECK(DeleteFileA(outside_path) != 0);
        CHECK(test_release_owned_directory(&root));
        CHECK(test_release_owned_directory(&outside));
        return;
    }

    alignas(16) uint8_t memory[512];
    Arena arena;
    Allocator alloc = test_arena_alloc(&arena, memory, sizeof(memory));
    PlatformFile file{};
    CHECK(platform_file_read_rooted(root.path, "inside.bin", 64u,
                                    alloc, &file));
    CHECK(file.size == std::strlen(inside) &&
          std::memcmp(file.data, inside, file.size) == 0);

    const size_t offset = arena.offset;
    PlatformFile untouched{(void*)(uintptr_t)0xC0FFEEu, 77u};
    CHECK(!platform_file_read_rooted(root.path, "inside.bin", 2u,
                                     alloc, &untouched));
    CHECK(untouched.data == (void*)(uintptr_t)0xC0FFEEu && untouched.size == 77u);
    CHECK(arena.offset == offset);

    const bool junction_created = test_create_junction(junction, outside.wide_path);
    CHECK(junction_created);
    if (!junction_created) {
        CHECK(DeleteFileA(inside_path) != 0);
        CHECK(DeleteFileA(outside_path) != 0);
        CHECK(test_release_owned_directory(&root));
        CHECK(test_release_owned_directory(&outside));
        return;
    }
    CHECK(!platform_file_read_rooted(root.path, "escape/payload.bin",
                                     64u, alloc, &untouched));
    CHECK(untouched.data == (void*)(uintptr_t)0xC0FFEEu && untouched.size == 77u);
    CHECK(arena.offset == offset);
    CHECK(!platform_file_read_rooted(root.path, "../payload.bin",
                                     64u, alloc, &untouched));
    CHECK(arena.offset == offset);
    const bool owned_hard_link_created = CreateHardLinkW(
        hard_link, outside_file_wide, nullptr) != 0;
    CHECK(owned_hard_link_created);
    CHECK(!platform_file_read_rooted(root.path, "outside-hardlink.bin",
                                     64u, alloc, &untouched));
    CHECK(untouched.data == (void*)(uintptr_t)0xC0FFEEu && untouched.size == 77u);
    CHECK(arena.offset == offset);

    CHECK(RemoveDirectoryW(junction) != 0);
    if (owned_hard_link_created) CHECK(DeleteFileW(hard_link) != 0);
    CHECK(DeleteFileA(inside_path) != 0);
    CHECK(DeleteFileA(outside_path) != 0);
    CHECK(test_release_owned_directory(&root));
    CHECK(test_release_owned_directory(&outside));
}

TEST(platform, owned_test_directory_scope_cannot_be_replaced_while_bound) {
    OwnedTestDirectory owned{};
    owned.scope_custody = INVALID_HANDLE_VALUE;
    owned.directory_custody = INVALID_HANDLE_VALUE;
    const bool created = test_create_owned_directory("custody", &owned);
    CHECK(created);
    if (!created) return;

    wchar_t moved[160];
    wchar_t moved_child[192];
    const int moved_chars = swprintf_s(moved, L"%s_moved", owned.wide_scope_path);
    const int moved_child_chars = swprintf_s(
        moved_child, L"%s\\work_moved", owned.wide_scope_path);
    CHECK(moved_chars > 0 && moved_child_chars > 0);
    if (moved_chars > 0 && moved_child_chars > 0) {
        CHECK(MoveFileExW(owned.wide_scope_path, moved, 0u) == 0);
        CHECK(MoveFileExW(owned.wide_path, moved_child, 0u) == 0);
        CHECK(GetFileAttributesW(owned.wide_scope_path) != INVALID_FILE_ATTRIBUTES);
        CHECK(GetFileAttributesW(owned.wide_path) != INVALID_FILE_ATTRIBUTES);
        CHECK(GetFileAttributesW(moved) == INVALID_FILE_ATTRIBUTES);
        CHECK(GetFileAttributesW(moved_child) == INVALID_FILE_ATTRIBUTES);
    }
    CHECK(test_release_owned_directory(&owned));
    CHECK(GetFileAttributesW(owned.wide_scope_path) == INVALID_FILE_ATTRIBUTES);
}

TEST(platform, file_write_then_read_roundtrip) {
    const char* path = "moba_io_roundtrip.bin";
    uint8_t src[1037];                                  // odd size: not page/sector aligned
    for (size_t i = 0; i < sizeof(src); ++i) src[i] = (uint8_t)(i * 31 + 7);
    CHECK(platform_file_write(path, src, sizeof(src)));

    alignas(16) uint8_t mem[4096];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};   // = {}: a failed read leaves out untouched, and CHECK does
                           // not stop the body — later CHECKs must read defined values
    CHECK(platform_file_read(path, al, &f));
    CHECK(f.size == sizeof(src));
    CHECK(((uintptr_t)f.data % 16) == 0);               // MEM_DEFAULT_ALIGN honored
    CHECK(f.size == sizeof(src) && std::memcmp(f.data, src, sizeof(src)) == 0);

    size_t stat_size = 0;                               // platform_file_size agrees
    CHECK(platform_file_size(path, &stat_size));
    CHECK(stat_size == sizeof(src));
    std::remove(path);
    CHECK(!platform_file_size(path, &stat_size));       // gone -> false
}

TEST(platform, file_read_missing_fails_and_leaves_out_untouched) {
    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f; f.data = (void*)(uintptr_t)0xC0FFEE; f.size = 123;
    CHECK(!platform_file_read("moba_io_does_not_exist.bin", al, &f));
    CHECK(f.data == (void*)(uintptr_t)0xC0FFEE && f.size == 123);
    CHECK(a.offset == 0);                               // nothing allocated on failure
}

TEST(platform, file_write_overwrite_replaces_and_removes_tmp) {
    const char* path = "moba_io_overwrite.bin";
    const char* first  = "first-content-which-is-longer";
    const char* second = "second";
    CHECK(platform_file_write(path, first, std::strlen(first)));
    CHECK(platform_file_write(path, second, std::strlen(second)));

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};
    CHECK(platform_file_read(path, al, &f));
    CHECK(f.size == std::strlen(second));               // shrank — no stale tail bytes
    CHECK(f.size == std::strlen(second) && std::memcmp(f.data, second, f.size) == 0);

    // The temp file must not survive a successful write (rename moved it).
    PlatformFile tmp;
    char tmp_path[64];
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    CHECK(!platform_file_read(tmp_path, al, &tmp));
    std::remove(path);
}

TEST(platform, file_write_refuses_preexisting_tmp_without_mutation) {
    const char* path = "moba_io_tmp_collision.bin";
    const char* tmp_path = "moba_io_tmp_collision.bin.tmp";
    const char* original = "original-destination";
    const char* sentinel = "attacker-owned-temporary";
    std::remove(path);
    std::remove(tmp_path);
    CHECK(platform_file_write(path, original, std::strlen(original)));

    FILE* tmp = std::fopen(tmp_path, "wb");
    CHECK(tmp != nullptr);
    if (tmp) {
        CHECK(std::fwrite(sentinel, 1, std::strlen(sentinel), tmp) == std::strlen(sentinel));
        std::fclose(tmp);
    }

    CHECK(!platform_file_write(path, "replacement", 11));

    alignas(16) uint8_t mem[512];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile destination{};
    PlatformFile temporary{};
    CHECK(platform_file_read(path, al, &destination));
    CHECK(platform_file_read(tmp_path, al, &temporary));
    CHECK(destination.size == std::strlen(original) &&
          std::memcmp(destination.data, original, destination.size) == 0);
    CHECK(temporary.size == std::strlen(sentinel) &&
          std::memcmp(temporary.data, sentinel, temporary.size) == 0);

    std::remove(tmp_path);
    std::remove(path);
}

struct PostFlushReplacementAttack {
    wchar_t attacker_path[MAX_PATH];
    wchar_t moved_path[MAX_PATH];
    bool called;
    bool temp_move_succeeded;
    bool attacker_move_succeeded;
    DWORD temp_move_error;
    DWORD attacker_move_error;
};

static void attempt_post_flush_temp_replacement(const wchar_t* temporary_path, void* user) {
    PostFlushReplacementAttack* attack = static_cast<PostFlushReplacementAttack*>(user);
    attack->called = true;
    SetLastError(ERROR_SUCCESS);
    attack->temp_move_succeeded = MoveFileExW(
        temporary_path, attack->moved_path, MOVEFILE_REPLACE_EXISTING) != 0;
    attack->temp_move_error = GetLastError();
    SetLastError(ERROR_SUCCESS);
    attack->attacker_move_succeeded = MoveFileExW(
        attack->attacker_path, temporary_path, MOVEFILE_REPLACE_EXISTING) != 0;
    attack->attacker_move_error = GetLastError();
}

TEST(platform, file_write_keeps_create_only_temp_bound_through_commit) {
    const char* path = "moba_io_bound_commit.bin";
    const wchar_t* attacker_name = L"moba_io_bound_commit.attacker";
    const wchar_t* moved_name = L"moba_io_bound_commit.moved";
    const char* payload = "trusted-post-flush-payload";
    const char* sentinel = "attacker-sentinel";
    std::remove(path);
    _wremove(L"moba_io_bound_commit.bin.tmp");
    _wremove(attacker_name);
    _wremove(moved_name);

    FILE* attacker = nullptr;
    CHECK(_wfopen_s(&attacker, attacker_name, L"wb") == 0 && attacker != nullptr);
    if (attacker) {
        CHECK(std::fwrite(sentinel, 1, std::strlen(sentinel), attacker) ==
              std::strlen(sentinel));
        std::fclose(attacker);
    }

    PostFlushReplacementAttack attack{};
    CHECK(GetFullPathNameW(
              attacker_name, MAX_PATH, attack.attacker_path, nullptr) > 0);
    CHECK(GetFullPathNameW(moved_name, MAX_PATH, attack.moved_path, nullptr) > 0);
    CHECK(win32_platform_file_write_with_hook(
        path, payload, std::strlen(payload), attempt_post_flush_temp_replacement,
        &attack));
    CHECK(attack.called);
    CHECK(!attack.temp_move_succeeded);
    CHECK(!attack.attacker_move_succeeded);
    CHECK(attack.temp_move_error == ERROR_SHARING_VIOLATION);
    CHECK(attack.attacker_move_error == ERROR_SHARING_VIOLATION ||
          attack.attacker_move_error == ERROR_ACCESS_DENIED);

    alignas(16) uint8_t mem[512];
    Arena arena;
    Allocator alloc = test_arena_alloc(&arena, mem, sizeof(mem));
    PlatformFile destination{};
    PlatformFile attacker_file{};
    CHECK(platform_file_read(path, alloc, &destination));
    CHECK(platform_file_read("moba_io_bound_commit.attacker", alloc, &attacker_file));
    CHECK(destination.size == std::strlen(payload) &&
          std::memcmp(destination.data, payload, destination.size) == 0);
    CHECK(attacker_file.size == std::strlen(sentinel) &&
          std::memcmp(attacker_file.data, sentinel, attacker_file.size) == 0);
    CHECK(GetFileAttributesW(L"moba_io_bound_commit.bin.tmp") == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(moved_name) == INVALID_FILE_ATTRIBUTES);

    std::remove(path);
    _wremove(attacker_name);
    _wremove(moved_name);
    _wremove(L"moba_io_bound_commit.bin.tmp");
}

TEST(platform, vulkan_sdk_loader_path_is_explicit_and_transactional) {
    wchar_t out[1024] = L"untouched";
    CHECK(!win32_vulkan_sdk_loader_path(nullptr, out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"C", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"C:", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"C:\\", nullptr, 1024));
    CHECK(std::wcscmp(out, L"untouched") == 0);
    CHECK(!win32_vulkan_sdk_loader_path(L".", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"C:relative", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"\\\\server\\share", out, 1024));
    CHECK(!win32_vulkan_sdk_loader_path(L"C:\\moba-sdk-path-that-does-not-exist", out, 1024));
    CHECK(std::wcscmp(out, L"untouched") == 0);

    const wchar_t* sdk_root = _wgetenv(L"VULKAN_SDK");
    CHECK(sdk_root != nullptr && sdk_root[0] != L'\0');
    if (sdk_root && sdk_root[0] != L'\0') {
        wchar_t too_small[8] = L"stable";
        CHECK(!win32_vulkan_sdk_loader_path(sdk_root, too_small, 8));
        CHECK(std::wcscmp(too_small, L"stable") == 0);
        CHECK(win32_vulkan_sdk_loader_path(sdk_root, out, 1024));
        CHECK(std::wcsstr(out, L"\\Bin\\vulkan-1.dll") != nullptr);
    }
}

TEST(platform, file_zero_byte_roundtrip) {
    const char* path = "moba_io_empty.bin";
    CHECK(platform_file_write(path, nullptr, 0));

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f; f.data = nullptr; f.size = 999;
    CHECK(platform_file_read(path, al, &f));
    CHECK(f.size == 0);
    CHECK(f.data != nullptr);                           // contract: valid pointer even at size 0
    std::remove(path);
}

// NOTE: there is deliberately no "file too big for the arena" case — fixed-arena
// overrun is a hard ENSURE abort by the M1.0 OOM policy, not a recoverable failure.
// Callers reading UNTRUSTED files bound the size first via platform_file_size (see
// the renderer's pipeline-cache load).

// The failure half of the atomicity contract: when the final rename fails (read-only
// destination -> MoveFileExW ERROR_ACCESS_DENIED), the original file must be intact,
// the .tmp removed, and the call must return false. This is the branch a "delete
// destination, then rename" regression would break while every success-path test
// stayed green.
TEST(platform, file_write_rename_failure_preserves_original) {
    const char* path = "moba_io_readonly.bin";
    const char* orig = "original-content";
    CHECK(platform_file_write(path, orig, std::strlen(orig)));
    CHECK(_chmod(path, _S_IREAD) == 0);                 // destination read-only

    CHECK(!platform_file_write(path, "replacement", 11));

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};
    CHECK(platform_file_read(path, al, &f));            // original intact, byte-for-byte
    CHECK(f.size == std::strlen(orig) && std::memcmp(f.data, orig, f.size) == 0);

    PlatformFile tmp = {};
    CHECK(!platform_file_read("moba_io_readonly.bin.tmp", al, &tmp));   // tmp cleaned up

    CHECK(_chmod(path, _S_IREAD | _S_IWRITE) == 0);
    std::remove(path);
}

// "Paths are UTF-8" (platform.h): a non-ASCII name must round-trip through the wide
// Win32 APIs. Source is compiled /utf-8, so the bytes below are literal UTF-8.
TEST(platform, file_utf8_path_roundtrip) {
    const char* path = "moba_io_\xC3\xA9\xE6\xB8\xAC.bin";   // "moba_io_é測.bin"
    const char* body = "utf8-path-bytes";
    CHECK(platform_file_write(path, body, std::strlen(body)));

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};
    CHECK(platform_file_read(path, al, &f));
    CHECK(f.size == std::strlen(body) && std::memcmp(f.data, body, f.size) == 0);

    // CRT remove() resolves through the ANSI code page — delete via the wide name.
    // ==0 also proves the file landed under the correctly-converted UTF-16 name.
    CHECK(_wremove(L"moba_io_é測.bin") == 0);
}

TEST(platform, file_invalid_utf8_path_rejected) {
    const char* bad = "moba_io_\xFF\xFE.bin";           // 0xFF never occurs in UTF-8
    CHECK(!platform_file_write(bad, "x", 1));           // MB_ERR_INVALID_CHARS rejects

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};
    CHECK(!platform_file_read(bad, al, &f));
    size_t sz = 0;
    CHECK(!platform_file_size(bad, &sz));
}

TEST(platform, file_write_to_missing_directory_fails_cleanly) {
    const char* path = "moba_io_no_such_dir/out.bin";
    CHECK(!platform_file_write(path, "x", 1));          // CreateFile on the .tmp fails

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f;
    CHECK(!platform_file_read(path, al, &f));           // nothing was created
}
