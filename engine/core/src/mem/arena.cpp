#include "core/mem.h"
#include <cstring>   // memcpy, memset

// ASan poison hooks (ready for the later Debug-ASan preset; no-ops without ASan).
#if defined(__SANITIZE_ADDRESS__)
    #include <sanitizer/asan_interface.h>
    #define ARENA_POISON(p, n)   __asan_poison_memory_region((p), (n))
    #define ARENA_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
    #define ARENA_POISON(p, n)   ((void)(p), (void)(n))
    #define ARENA_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

void arena_init(Arena* a, void* base, size_t reserved, size_t committed,
                ArenaCommitFn commit, size_t commit_granularity) {
    a->base               = (uint8_t*)base;
    a->offset             = 0;
    a->committed          = committed;
    a->reserved           = reserved;
    a->high_water         = 0;
    a->commit             = commit;
    a->commit_granularity = commit_granularity;
    // Poison only VIRTUAL arenas (the engine's heap-backed frame/world memory).
    // Fixed caller-owned buffers (often stack, e.g. test fixtures) must not be
    // poisoned: ASan leaves user-poison in place across stack-frame reuse, so any
    // later whole-struct write (e.g. `Fixture f{}`) would false-positive. The
    // caller's own zero-init defines freshness for fixed buffers.
    if (a->commit) ARENA_POISON(a->base, a->committed);   // dead until pushed
}

void arena_init_fixed(Arena* a, void* buffer, size_t size) {
    arena_init(a, buffer, size, size, nullptr, 0);
}

void* arena_push(Arena* a, size_t size, size_t align) {
    ASSERT_ALWAYS(align != 0 && (align & (align - 1)) == 0);   // power of two
    // Align the ABSOLUTE address so the result is aligned regardless of base.
    uintptr_t base_addr = (uintptr_t)a->base;
    uintptr_t aligned   = (base_addr + a->offset + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t    start     = (size_t)(aligned - base_addr);
    size_t    end       = start + size;

    ENSURE_MSG(end <= a->reserved, "arena overrun (reserved budget exceeded)");
    if (end > a->committed) {
        ENSURE_MSG(a->commit != nullptr, "arena overrun (fixed buffer, no commit fn)");
        size_t gran = a->commit_granularity ? a->commit_granularity : 1;
        size_t want = ((end + gran - 1) / gran) * gran;
        if (want > a->reserved) want = a->reserved;
        size_t old_committed = a->committed;
        ENSURE_MSG(a->commit(a->base, want), "page commit failed");
        ARENA_POISON(a->base + old_committed, want - old_committed);
        a->committed = want;
    }

    a->offset = end;
    if (end > a->high_water) a->high_water = end;
    ARENA_UNPOISON(a->base + start, size);
    return a->base + start;
}

void* arena_push_zero(Arena* a, size_t size, size_t align) {
    void* p = arena_push(a, size, align);
    memset(p, 0, size);
    return p;
}

void arena_reset(Arena* a) {
    if (a->commit) ARENA_POISON(a->base, a->committed);   // virtual arenas only
    a->offset = 0;                         // committed + high_water retained
}

static void* arena_alloc_fn(void* state, void* ptr, size_t old_size, size_t new_size, size_t align) {
    Arena* a = (Arena*)state;
    if (new_size == 0) return nullptr;     // free: arenas reclaim in bulk -> no-op
    if (align == 0) align = MEM_DEFAULT_ALIGN;
    void* np = arena_push(a, new_size, align);
    if (ptr && old_size) {                 // realloc: copy the smaller of old/new
        memcpy(np, ptr, old_size < new_size ? old_size : new_size);
    }
    return np;
}

Allocator arena_allocator(Arena* a) {
    Allocator al;
    al.fn = arena_alloc_fn;
    al.state = a;
    al.kind = ALLOC_ARENA;
    return al;
}

TempMemory temp_begin(Arena* a) {
    TempMemory t;
    t.arena = a;
    t.saved_offset = a->offset;
    return t;
}

void temp_end(TempMemory t) {
    if (t.arena->commit) ARENA_POISON(t.arena->base + t.saved_offset,
                                      t.arena->offset - t.saved_offset);
    t.arena->offset = t.saved_offset;
}

Arena* scratch_current(ScratchPad* s) { return &s->a[s->cur]; }

void scratch_next_frame(ScratchPad* s) {
    s->cur ^= 1;
    arena_reset(&s->a[s->cur]);
}
