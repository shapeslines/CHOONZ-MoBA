#pragma once
#include <stdint.h>
#include <stddef.h>
#include "platform/platform_input.h"
#include "core/mem.h"     // Arena / ScratchPad (platform wires OS pages into them)
// THE OS seam (ADR-0005, ARCHITECTURE §4). Engine modules include only this
// (+ platform_input.h, and later platform_vulkan.h). Win32 impl in src/win32/.
//
// M0.3 surface: window/loop, high-res clock, OS page allocator, log/fatal.
// M2.1 adds file read/write (the renderer loads .spv + the pipeline cache).
// Deferred to their phases (declared canonically in ARCHITECTURE §4.1):
//   - file map/unmap (immutable assets) + asset-root vpaths  -> Phase 4
//   - UDP sockets, dir-watch                                 -> Phase 6 / later

typedef struct PlatformWindow PlatformWindow;     // opaque, per-backend
typedef struct PlatformWindowDesc {
    const char* title;
    int32_t     width, height;
    bool        resizable;
    bool        fullscreen;
} PlatformWindowDesc;

// ---- Window / loop ----
PlatformWindow* platform_window_open(const PlatformWindowDesc*);
void            platform_window_close(PlatformWindow*);
bool            platform_pump_events(PlatformWindow*, PlatformFrameInput* out); // false => quit
void            platform_window_size(PlatformWindow*, int32_t* w, int32_t* h);

// ---- High-res monotonic clock (raw ticks; seconds computed engine-side) ----
uint64_t platform_time_ticks(void);          // QueryPerformanceCounter
uint64_t platform_time_frequency(void);
double   platform_time_seconds(uint64_t ticks);
void     platform_sleep_ms(uint32_t ms);

// ---- OS page allocator (the ONLY thing the memory subsystem depends on, §6) ----
typedef struct PlatformMemoryBlock { void* base; size_t committed; size_t reserved; } PlatformMemoryBlock;
PlatformMemoryBlock plat_mem_reserve(size_t reserve_bytes);   // MEM_RESERVE
bool                plat_mem_commit (PlatformMemoryBlock*, size_t new_committed);
void                plat_mem_release(PlatformMemoryBlock*);
size_t              plat_mem_page_size(void);

// ---- OS-page-backed arenas (bridges plat_mem into a core Arena; ADR-0005) ----
// Reserves `reserve_bytes` of address space; the arena commits pages on growth.
bool platform_arena_reserve     (Arena* out, size_t reserve_bytes);
bool platform_scratchpad_reserve(ScratchPad* out, size_t each_bytes);
void platform_arena_release     (Arena*);   // VirtualFree the arena's reservation

// ---- File I/O: caller supplies the allocator (the arena owns the bytes, §4.1) ----
// Paths are UTF-8. platform_file_read allocates size bytes (16-aligned) from `alloc`
// and fills `out`; a 0-byte file yields size==0 with a valid (non-null) data pointer.
// Returns false (out untouched) if the file is missing/unreadable. The caller frees by
// the allocator's own discipline (arena reset/temp_end — there is no per-file free).
typedef struct PlatformFile { void* data; size_t size; } PlatformFile;
// Cheap stat (no open/alloc). Use it to BOUND a read of untrusted/external bytes
// before pushing into a fixed arena — arena overrun is a hard abort by the M1.0 OOM
// policy, and an on-disk file's size must never be able to trigger it.
bool platform_file_size (const char* path, size_t* out_size);
bool platform_file_read (const char* path, Allocator alloc, PlatformFile* out);
// Same-handle bounded read for externally mutable files. The opened file's size is
// checked against `max_bytes` before allocation and the same handle supplies all
// bytes. On failure both `out` and the allocator remain untouched.
bool platform_file_read_bounded(const char* path, size_t max_bytes,
                                Allocator alloc, PlatformFile* out);
// Asset-root read: `relative_path` is a forward-slash relative path. The platform
// opens and binds every component without write/delete sharing, rejects reparse
// points, multi-link file aliases, and root escapes, derives size from the final
// open handle, enforces `max_bytes` before allocation, and reads that same handle.
bool platform_file_read_rooted(const char* root, const char* relative_path,
                               size_t max_bytes, Allocator alloc,
                               PlatformFile* out);
// Atomic whole-file write: creates a new path + ".tmp", flushes, then renames the
// still-open temporary handle over `path` (readers never observe a torn file). A
// pre-existing .tmp is never overwritten; that collision returns false and leaves
// both files untouched.
bool platform_file_write(const char* path, const void* data, size_t size);
// Root-confined atomic write. Every existing parent component is held open without
// following reparses or permitting write/delete sharing through the handle-bound
// commit. Parent directories must already exist. The final file may be created or
// replaced; a pre-existing predictable `.tmp` still fails closed.
bool platform_file_write_rooted(const char* root, const char* relative_path,
                                const void* data, size_t size);
// Root-confined removal for a regular, single-link file. The root and every
// component are handle-bound without following reparses; deletion is applied to
// the verified final handle. A missing final file is already removed and succeeds.
bool platform_file_remove_rooted(const char* root, const char* relative_path);

// ---- Diagnostics ----
void platform_log  (const char* fmt, ...);
[[noreturn]] void platform_fatal(const char* fmt, ...); // logs, then aborts the process
