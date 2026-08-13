// M1.0 memory tests — arenas, allocator, scratch. Self-registering (test.h, M1.4).
#include "test.h"
#include "core/mem.h"
#include "core/assert.h"
#include "platform/platform.h"
#include <cstdint>

TEST(mem, fixed_arena_align_distinct_highwater) {
    alignas(64) uint8_t buf[4096];
    Arena a; arena_init_fixed(&a, buf, sizeof(buf));
    void* p1 = arena_push(&a, 1, 16);
    void* p2 = arena_push(&a, 1, 16);
    void* p3 = arena_push(&a, 8, 64);
    CHECK(((uintptr_t)p1 % 16) == 0);
    CHECK(((uintptr_t)p2 % 16) == 0);
    CHECK(((uintptr_t)p3 % 64) == 0);
    CHECK(p1 != p2 && p2 != p3);
    CHECK(a.offset > 0 && a.high_water == a.offset);
}

TEST(mem, reset_clears_offset_keeps_highwater) {
    alignas(16) uint8_t buf[256];
    Arena a; arena_init_fixed(&a, buf, sizeof(buf));
    void* p1 = arena_push(&a, 32, 16);
    size_t hw = a.high_water;
    arena_reset(&a);
    CHECK(a.offset == 0);
    CHECK(a.high_water == hw);
    void* p2 = arena_push(&a, 32, 16);
    CHECK(p2 == p1);                                 // memory reused after reset
}

TEST(mem, temp_memory_save_restore) {
    alignas(16) uint8_t buf[256];
    Arena a; arena_init_fixed(&a, buf, sizeof(buf));
    arena_push(&a, 16, 16);
    size_t before = a.offset;
    TempMemory t = temp_begin(&a);
    arena_push(&a, 64, 16);
    CHECK(a.offset > before);
    temp_end(t);
    CHECK(a.offset == before);
}

// ASan poison lifecycle (G10): fixed-buffer arenas must stay ASan-clean across
// reset/push reuse (no false positives), and VIRTUAL arenas must survive
// init-poison → push-unpoison → reset-poison → re-push cycles with no reports.
// Under the debug-asan preset this test is the poison regression net; without
// ASan it is a plain reuse regression test.
TEST(mem, asan_poison_lifecycle_roundtrip) {
    alignas(64) uint8_t buf[4096];
    Arena a; arena_init_fixed(&a, buf, sizeof(buf));
    uint8_t* p = (uint8_t*)arena_push(&a, 256, 64);
    for (int i = 0; i < 256; ++i) p[i] = (uint8_t)i;
    TempMemory t = temp_begin(&a);
    arena_push(&a, 128, 64);
    temp_end(t);
    CHECK(a.offset == 256);
    size_t hw = a.high_water;                       // high_water is monotonic: 384
    arena_reset(&a);
    CHECK(a.offset == 0 && a.high_water == hw);
    uint8_t* q = (uint8_t*)arena_push(&a, 256, 64);
    CHECK(q == p);                                  // memory reused after reset
    for (int i = 0; i < 256; ++i) q[i] = 0;         // freshly unpoisoned + writable
    arena_reset(&a);
    arena_push(&a, 4096, 64);                       // full-size reuse, aligned
    CHECK(a.offset == 4096);

    Arena v;
    CHECK(platform_arena_reserve(&v, (size_t)1 << 20));   // virtual: poison path live
    if (v.base) {
        uint8_t* w = (uint8_t*)arena_push(&v, 4096, 64);  // unpoisons [0,4096)
        for (int i = 0; i < 4096; ++i) w[i] = (uint8_t)(i & 0xFF);
        arena_reset(&v);                                  // re-poisons [0,committed)
        CHECK(v.offset == 0);
        uint8_t* r = (uint8_t*)arena_push(&v, 1024, 64);  // unpoisons [0,1024)
        for (int i = 0; i < 1024; ++i) r[i] = 0;
        platform_arena_release(&v);
    }
}

TEST(mem, allocator_wrapper_over_arena) {
    alignas(16) uint8_t buf[256];
    Arena a; arena_init_fixed(&a, buf, sizeof(buf));
    Allocator al = arena_allocator(&a);
    void* p = mem_alloc(al, 40, 16);
    CHECK(p != nullptr && ((uintptr_t)p % 16) == 0);
    mem_free(al, p, 40);                             // no-op for arenas; must not crash
}

TEST(mem, virtual_arena_commits_only_touched_pages) {
    Arena a;
    const size_t RES = (size_t)256 * 1024 * 1024;    // 256 MB reserved
    CHECK(platform_arena_reserve(&a, RES));
    if (a.base) {
        arena_push(&a, 4096, 16);                    // touch a little
        CHECK(a.reserved == RES);
        CHECK(a.committed >= 4096);
        CHECK(a.committed <= (size_t)4 * 1024 * 1024);   // committed << reserved (the DoD)
        std::printf("  virtual arena: committed=%zu KB of reserved=%zu MB\n",
                    a.committed / 1024, a.reserved / (1024 * 1024));
        platform_arena_release(&a);
    }
}

TEST(mem, scratch_double_buffer_swap_resets) {
    ScratchPad s;
    CHECK(platform_scratchpad_reserve(&s, (size_t)1 << 20));   // 1 MB each
    Arena* f0 = scratch_current(&s);
    arena_push(f0, 100, 16);
    CHECK(f0->offset > 0);
    scratch_next_frame(&s);
    Arena* f1 = scratch_current(&s);
    CHECK(f1 != f0 && f1->offset == 0);
    platform_arena_release(&s.a[0]);
    platform_arena_release(&s.a[1]);
}
