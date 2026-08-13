// Win32 OS page allocator + OS-page-backed arenas (ADR-0005). Split out of
// win32_platform.cpp (G14) so the headless test group (engine_core_group) links a
// window-free object: static-lib linking pulls objects per referenced symbol, and
// tests reference only this TU (and win32_file.cpp / win32_log.cpp), never the
// window TU with its windows.h + User32/Gdi32 surface.
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "core/mem.h"
#include "platform/platform.h"

// ---- OS page allocator ------------------------------------------------------
size_t plat_mem_page_size(void) {
    SYSTEM_INFO si; GetSystemInfo(&si); return (size_t)si.dwPageSize;
}
PlatformMemoryBlock plat_mem_reserve(size_t reserve_bytes) {
    PlatformMemoryBlock b; b.base = VirtualAlloc(nullptr, reserve_bytes, MEM_RESERVE, PAGE_NOACCESS);
    b.committed = 0; b.reserved = b.base ? reserve_bytes : 0;
    return b;
}
bool plat_mem_commit(PlatformMemoryBlock* b, size_t new_committed) {
    if (!b || !b->base || new_committed > b->reserved) return false;
    if (new_committed <= b->committed) return true;
    if (!VirtualAlloc(b->base, new_committed, MEM_COMMIT, PAGE_READWRITE)) return false;
    b->committed = new_committed;
    return true;
}
void plat_mem_release(PlatformMemoryBlock* b) {
    if (b && b->base) { VirtualFree(b->base, 0, MEM_RELEASE); b->base = nullptr; b->committed = 0; b->reserved = 0; }
}

// ---- OS-page-backed arenas: the commit callback core's Arena injects (ADR-0005) ----
static bool win32_arena_commit(void* base, size_t new_committed) {
    return VirtualAlloc(base, new_committed, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}
bool platform_arena_reserve(Arena* out, size_t reserve_bytes) {
    PlatformMemoryBlock blk = plat_mem_reserve(reserve_bytes);
    if (!blk.base) return false;
    arena_init(out, blk.base, blk.reserved, 0, win32_arena_commit, plat_mem_page_size());
    return true;
}
bool platform_scratchpad_reserve(ScratchPad* out, size_t each_bytes) {
    if (!platform_arena_reserve(&out->a[0], each_bytes)) return false;
    if (!platform_arena_reserve(&out->a[1], each_bytes)) { platform_arena_release(&out->a[0]); return false; }
    out->cur = 0;
    return true;
}
void platform_arena_release(Arena* a) {
    if (a && a->base) {
        VirtualFree(a->base, 0, MEM_RELEASE);
        a->base = nullptr; a->offset = 0; a->committed = 0; a->reserved = 0;
    }
}
