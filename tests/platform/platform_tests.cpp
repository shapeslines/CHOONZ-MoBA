// M2.1 platform file I/O tests — read-into-allocator + atomic whole-file write
// (ARCHITECTURE §4.1). Runs headlessly; files land in the CTest working dir and are
// removed at the end of each case (remove() — the seam has no delete on purpose).
#include "test.h"
#include "core/mem.h"
#include "platform/platform.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <io.h>         // _chmod (force a rename failure via a read-only destination)
#include <sys/stat.h>   // _S_IREAD / _S_IWRITE

static Allocator test_arena_alloc(Arena* a, uint8_t* buf, size_t n) {
    arena_init_fixed(a, buf, n);
    return arena_allocator(a);
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

TEST(platform, file_write_refuses_preexisting_tmp) {
    const char* path = "moba_io_tmp_collision.bin";
    const char* tmp_path = "moba_io_tmp_collision.bin.tmp";
    const char* sentinel = "do-not-overwrite";
    std::remove(path);
    std::remove(tmp_path);

    FILE* tmp = std::fopen(tmp_path, "wb");
    CHECK(tmp != nullptr);
    if (tmp) {
        CHECK(std::fwrite(sentinel, 1, std::strlen(sentinel), tmp) == std::strlen(sentinel));
        std::fclose(tmp);
    }

    CHECK(!platform_file_write(path, "replacement", 11));

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_arena_alloc(&a, mem, sizeof(mem));
    PlatformFile f = {};
    CHECK(platform_file_read(tmp_path, al, &f));
    CHECK(f.size == std::strlen(sentinel) &&
          std::memcmp(f.data, sentinel, f.size) == 0);
    size_t final_size = 0;
    CHECK(!platform_file_size(path, &final_size));

    std::remove(tmp_path);
    std::remove(path);
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
