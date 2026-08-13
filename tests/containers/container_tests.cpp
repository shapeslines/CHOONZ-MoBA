// M1.3 container tests — thorough. Array / InlineArray / Str / Pool / HandlePool /
// HashMap, including the handle-staleness DoD and a HashMap stress that hammers
// Robin-Hood probing + backward-shift delete. Self-registering (test.h, M1.4).
#include "test.h"
#include "core/array.h"
#include "core/inline_array.h"
#include "core/str.h"
#include "core/pool.h"
#include "core/hashmap.h"
#include "platform/platform.h"
#include <cstdint>

struct Obj { uint32_t a, b; };   // >= 4 bytes for the intrusive free list

static void set_free_link(Obj* slot, uint32_t next) {
    memcpy(slot, &next, sizeof(next));
}

// Per-test backing arena: reserve 64 MB of address space (pages commit lazily), give
// out an Allocator, release on scope exit. No exceptions are thrown, so the dtor is
// safe under /EHs-c-.
struct TestArena {
    Arena arena{};   // zero-init: a failed reserve leaves base==null, so the dtor's
                     // platform_arena_release is a safe no-op (no read of indeterminate base)
    Allocator al;
    TestArena() {
        CHECK(platform_arena_reserve(&arena, (size_t)64 * 1024 * 1024));
        al = arena_allocator(&arena);
    }
    ~TestArena() { platform_arena_release(&arena); }
};

// Collect `n` u32 keys whose home bucket (hashmap_hash(k) & mask) == target, to
// force a deterministic collision chain in a HashMap of capacity mask+1.
static uint32_t collect_keys(uint32_t target, uint32_t mask, uint32_t n, uint32_t* out) {
    uint32_t found = 0;
    for (uint32_t k = 1; k < 50000000u && found < n; ++k)
        if ((uint32_t)(hashmap_hash(k) & mask) == target) out[found++] = k;
    return found;
}

TEST(containers, array) {
    TestArena ta; Allocator al = ta.al;
    Array<int> a; array_init(&a, al);
    CHECK(a.len == 0 && a.data == nullptr);
    for (int i = 0; i < 1000; ++i) array_push(&a, i * 3);
    CHECK(a.len == 1000);
    CHECK(a.cap >= 1000);
    bool ok = true; for (int i = 0; i < 1000; ++i) ok = ok && (a.data[i] == i * 3);
    CHECK(ok);
    CHECK(array_pop(&a) == 999 * 3);
    CHECK(a.len == 999);
    // remove_swap moves the last element into the hole
    Array<int> b; array_init(&b, al);
    array_push(&b, 10); array_push(&b, 20); array_push(&b, 30); array_push(&b, 40);
    array_remove_swap(&b, 1);                    // remove 20 -> [10,40,30]
    CHECK(b.len == 3 && b.data[0] == 10 && b.data[1] == 40 && b.data[2] == 30);
    array_clear(&b); CHECK(b.len == 0 && array_empty(&b));
    array_free(&a); array_free(&b);
    CHECK(a.data == nullptr && a.cap == 0);
}

TEST(containers, inline_array) {
    InlineArray<int, 4> a; inline_array_init(&a);
    CHECK(inline_array_cap(&a) == 4 && !inline_array_full(&a));
    inline_array_push(&a, 1); inline_array_push(&a, 2); inline_array_push(&a, 3); inline_array_push(&a, 4);
    CHECK(a.len == 4 && inline_array_full(&a));
    inline_array_remove_swap(&a, 0);             // [4,2,3]
    CHECK(a.len == 3 && a.data[0] == 4);
    CHECK(inline_array_pop(&a) == 3);
    inline_array_clear(&a); CHECK(a.len == 0);
}

TEST(containers, str_strview) {
    TestArena ta; Allocator al = ta.al;
    CHECK(strview_eq(strview_cstr("hero"), strview_cstr("hero")));
    CHECK(!strview_eq(strview_cstr("hero"), strview_cstr("herox")));
    CHECK(!strview_eq(strview_cstr("abc"), strview_cstr("abd")));
    Str s; str_init(&s, al);
    str_set(&s, strview_cstr("unit"));
    CHECK(s.len == 4 && strview_eq(str_view(&s), strview_cstr("unit")));
    str_append(&s, strview_cstr("s/hero"));
    CHECK(strview_eq(str_view(&s), strview_cstr("units/hero")));
    Str t; str_init(&t, al); str_set(&t, str_view(&s));
    CHECK(str_eq(&s, &t));
    str_free(&s); str_free(&t);
    CHECK(s.data == nullptr);
}

TEST(containers, pool_basics) {
    TestArena ta; Allocator al = ta.al;
    Pool<Obj> p; pool_init(&p, al, 4);
    CHECK(p.count == 0);
    uint32_t i0 = pool_alloc(&p), i1 = pool_alloc(&p), i2 = pool_alloc(&p);
    CHECK(i0 != POOL_INVALID && i1 != POOL_INVALID && i2 != POOL_INVALID);
    CHECK(i0 != i1 && i1 != i2 && i0 != i2);
    CHECK(p.count == 3);
    pool_at(&p, i1)->a = 42;
    CHECK(pool_at(&p, i1)->a == 42);
    pool_free(&p, i1); CHECK(p.count == 2);
    uint32_t i3 = pool_alloc(&p); CHECK(i3 == i1);     // reuses freed slot
    uint32_t i4 = pool_alloc(&p);                       // 4th live (cap 4)
    CHECK(i4 != POOL_INVALID);
    CHECK(pool_alloc(&p) == POOL_INVALID);              // exhausted
    pool_free_all(&p);
}

TEST(containers, handlepool_staleness) {
    TestArena ta; Allocator al = ta.al;
    HandlePool<Obj> hp; handlepool_init(&hp, al, 8);
    CHECK(handlepool_get(&hp, HANDLE_NULL) == nullptr);     // gen-0/null sentinel
    Obj o; o.a = 7; o.b = 9;
    Handle h1 = handlepool_alloc(&hp, o);
    CHECK(!handle_is_null(h1) && handle_gen(h1) == 1);
    CHECK(handlepool_valid(&hp, h1));
    Obj* p = handlepool_get(&hp, h1);
    CHECK(p && p->a == 7 && p->b == 9);
    p->a = 100; CHECK(handlepool_get(&hp, h1)->a == 100);

    uint32_t idx1 = handle_index(h1);
    CHECK(handlepool_free(&hp, h1));
    CHECK(!handlepool_valid(&hp, h1));
    CHECK(handlepool_get(&hp, h1) == nullptr);              // stale -> null, no crash
    CHECK(!handlepool_free(&hp, h1));                       // double free rejected

    Obj o2; o2.a = 1; o2.b = 2;
    Handle h2 = handlepool_alloc(&hp, o2);                  // reuses the slot
    CHECK(handle_index(h2) == idx1);                        // same slot index
    CHECK(h2 != h1);                                        // different generation
    CHECK(handle_gen(h2) == 2);
    CHECK(handlepool_get(&hp, h1) == nullptr);              // old handle still stale
    CHECK(handlepool_get(&hp, h2) != nullptr);             // new handle valid

    // many cycles on the same slot, generation increments, no corruption
    Handle h = h2;
    for (int i = 0; i < 50; ++i) { handlepool_free(&hp, h); Obj x; x.a = (uint32_t)i; x.b = 0; h = handlepool_alloc(&hp, x); CHECK(handlepool_valid(&hp, h)); }
    CHECK(handlepool_get(&hp, h)->a == 49);

    // exhaustion -> HANDLE_NULL
    HandlePool<Obj> hp2; handlepool_init(&hp2, al, 2);
    Obj z{}; Handle a1 = handlepool_alloc(&hp2, z), a2 = handlepool_alloc(&hp2, z);
    CHECK(!handle_is_null(a1) && !handle_is_null(a2));
    CHECK(handle_is_null(handlepool_alloc(&hp2, z)));       // full
    handlepool_free_all(&hp2);
    handlepool_free_all(&hp);
}

TEST(containers, hashmap_basics) {
    TestArena ta; Allocator al = ta.al;
    HashMap<uint32_t, uint32_t> m; hashmap_init(&m, al);
    CHECK(hashmap_get(&m, 5u) == nullptr);                  // empty
    CHECK(!hashmap_remove(&m, 5u));
    hashmap_set(&m, 5u, 50u);
    hashmap_set(&m, 7u, 70u);
    CHECK(m.count == 2);
    CHECK(hashmap_get(&m, 5u) && *hashmap_get(&m, 5u) == 50u);
    CHECK(hashmap_has(&m, 7u) && !hashmap_has(&m, 9u));
    hashmap_set(&m, 5u, 555u);                              // update, not insert
    CHECK(m.count == 2 && *hashmap_get(&m, 5u) == 555u);
    CHECK(hashmap_remove(&m, 5u));
    CHECK(m.count == 1 && hashmap_get(&m, 5u) == nullptr);
    CHECK(*hashmap_get(&m, 7u) == 70u);                     // survivor intact
    CHECK(!hashmap_remove(&m, 5u));                         // already gone
    hashmap_free(&m);
}

TEST(containers, hashmap_stress_grow_probe_delete) {
    TestArena ta; Allocator al = ta.al;
    HashMap<uint32_t, uint32_t> m; hashmap_init(&m, al);
    const uint32_t N = 5000;
    for (uint32_t k = 0; k < N; ++k) hashmap_set(&m, k * 2654435761u + 1u, k);   // scattered keys
    CHECK(m.count == N);
    bool all = true; for (uint32_t k = 0; k < N; ++k) { uint32_t* v = hashmap_get(&m, k * 2654435761u + 1u); all = all && v && *v == k; }
    CHECK(all);
    // remove every other key (heavy backward-shift), survivors must stay findable
    for (uint32_t k = 0; k < N; k += 2) CHECK(hashmap_remove(&m, k * 2654435761u + 1u));
    CHECK(m.count == N / 2);
    bool kept = true, gone = true;
    for (uint32_t k = 0; k < N; ++k) {
        uint32_t* v = hashmap_get(&m, k * 2654435761u + 1u);
        if (k & 1) kept = kept && v && *v == k;             // odd: still present
        else       gone = gone && (v == nullptr);           // even: removed
    }
    CHECK(kept); CHECK(gone);
    // re-insert removed keys -> all present again
    for (uint32_t k = 0; k < N; k += 2) hashmap_set(&m, k * 2654435761u + 1u, k);
    CHECK(m.count == N);
    bool again = true; for (uint32_t k = 0; k < N; ++k) { uint32_t* v = hashmap_get(&m, k * 2654435761u + 1u); again = again && v && *v == k; }
    CHECK(again);
    hashmap_free(&m);
}

TEST(containers, hashmap_strview_and_u64_keys) {
    TestArena ta; Allocator al = ta.al;
    HashMap<StrView, int> m; hashmap_init(&m, al);
    hashmap_set(&m, strview_cstr("hero"),   1);
    hashmap_set(&m, strview_cstr("minion"), 2);
    hashmap_set(&m, strview_cstr("tower"),  3);
    CHECK(*hashmap_get(&m, strview_cstr("minion")) == 2);
    CHECK(hashmap_get(&m, strview_cstr("nexus")) == nullptr);
    CHECK(hashmap_remove(&m, strview_cstr("hero")));
    CHECK(hashmap_get(&m, strview_cstr("hero")) == nullptr);
    CHECK(*hashmap_get(&m, strview_cstr("tower")) == 3);
    hashmap_free(&m);

    HashMap<uint64_t, uint64_t> m2; hashmap_init(&m2, al);
    for (uint64_t k = 1; k <= 500; ++k) hashmap_set(&m2, k * 1000000007ULL, k);
    bool ok = true; for (uint64_t k = 1; k <= 500; ++k) { uint64_t* v = hashmap_get(&m2, k * 1000000007ULL); ok = ok && v && *v == k; }
    CHECK(ok);
    hashmap_free(&m2);
}

TEST(containers, hashmap_collision_chain_wraparound) {
    TestArena ta; Allocator al = ta.al;
    // Several keys sharing one home bucket form a contiguous probe run.
    HashMap<uint32_t, uint32_t> m; hashmap_init(&m, al);
    uint32_t k[5]; CHECK(collect_keys(7u, 15u, 5, k) == 5);
    for (uint32_t i = 0; i < 5; ++i) hashmap_set(&m, k[i], i * 10u + 1u);
    CHECK(m.cap == 16);                                       // no grow yet (5 < 7/8 of 16)
    for (uint32_t i = 0; i < 5; ++i) CHECK(hashmap_get(&m, k[i]) && *hashmap_get(&m, k[i]) == i * 10u + 1u);
    CHECK(hashmap_remove(&m, k[2]));                          // delete mid-chain -> backward-shift
    CHECK(hashmap_get(&m, k[2]) == nullptr);
    for (uint32_t i = 0; i < 5; ++i) if (i != 2) CHECK(hashmap_get(&m, k[i]) && *hashmap_get(&m, k[i]) == i * 10u + 1u);
    hashmap_free(&m);
    // Home == mask (15): colliders land at 15, 0, 1 -> deleting slot 15 shifts across the wrap.
    HashMap<uint32_t, uint32_t> w; hashmap_init(&w, al);
    uint32_t wk[3]; CHECK(collect_keys(15u, 15u, 3, wk) == 3);
    for (uint32_t i = 0; i < 3; ++i) hashmap_set(&w, wk[i], 100u + i);
    CHECK(hashmap_remove(&w, wk[0]));
    CHECK(hashmap_get(&w, wk[0]) == nullptr);
    CHECK(hashmap_get(&w, wk[1]) && *hashmap_get(&w, wk[1]) == 101u);   // wrapped members still findable
    CHECK(hashmap_get(&w, wk[2]) && *hashmap_get(&w, wk[2]) == 102u);
    hashmap_free(&w);
}

TEST(containers, hashmap_clear_empty_grow_boundary) {
    TestArena ta; Allocator al = ta.al;
    HashMap<uint32_t, uint32_t> m; hashmap_init(&m, al);
    for (uint32_t k = 0; k < 200; ++k) hashmap_set(&m, k, k * 2u);
    uint32_t capBefore = m.cap;
    hashmap_clear(&m);
    CHECK(m.count == 0 && m.cap == capBefore);               // buffer retained, no realloc
    for (uint32_t k = 0; k < 5; ++k) CHECK(hashmap_get(&m, k) == nullptr);
    for (uint32_t k = 0; k < 50; ++k) hashmap_set(&m, k, k + 1u);
    CHECK(m.count == 50);
    for (uint32_t k = 0; k < 50; ++k) CHECK(*hashmap_get(&m, k) == k + 1u);
    hashmap_clear(&m); hashmap_clear(&m); CHECK(m.count == 0);   // twice is fine
    hashmap_free(&m);
    HashMap<uint32_t, uint32_t> e; hashmap_init(&e, al); hashmap_clear(&e); CHECK(e.count == 0); hashmap_free(&e); // cap==0 no-op

    HashMap<uint32_t, uint32_t> r; hashmap_init(&r, al);
    hashmap_set(&r, 1u, 1u); hashmap_set(&r, 2u, 2u); hashmap_set(&r, 3u, 3u);
    CHECK(hashmap_remove(&r, 2u) && r.count == 2);
    CHECK(hashmap_remove(&r, 1u) && r.count == 1);
    CHECK(hashmap_remove(&r, 3u) && r.count == 0);
    CHECK(hashmap_get(&r, 1u) == nullptr);
    hashmap_set(&r, 9u, 99u); CHECK(*hashmap_get(&r, 9u) == 99u);   // works after empty
    hashmap_free(&r);

    HashMap<uint32_t, uint32_t> g; hashmap_init(&g, al);
    for (uint32_t k = 0; k < 13; ++k) hashmap_set(&g, k, k);
    CHECK(g.cap == 16);                                      // stays 16 through 13 inserts
    hashmap_set(&g, 13u, 13u);                               // (13+1)*8 >= 16*7 -> grow
    CHECK(g.cap == 32);
    for (uint32_t k = 0; k <= 13; ++k) CHECK(*hashmap_get(&g, k) == k);   // survive rehash
    hashmap_free(&g);
}

TEST(containers, hashmap_negative_keys_churn) {
    TestArena ta; Allocator al = ta.al;
    HashMap<int32_t, int> m; hashmap_init(&m, al);
    hashmap_set(&m, -1, 10); hashmap_set(&m, INT32_MIN, 20); hashmap_set(&m, 5, 30); hashmap_set(&m, -999, 40);
    CHECK(*hashmap_get(&m, -1) == 10);
    CHECK(*hashmap_get(&m, INT32_MIN) == 20);
    CHECK(*hashmap_get(&m, -999) == 40);
    CHECK(hashmap_get(&m, 999) == nullptr);
    CHECK(hashmap_remove(&m, INT32_MIN) && hashmap_get(&m, INT32_MIN) == nullptr);
    for (int i = 0; i < 100; ++i) hashmap_set(&m, 7, i);     // churn: 1 insert + 99 updates
    CHECK(m.count == 4 && *hashmap_get(&m, 7) == 99);        // -1,5,-999 (INT32_MIN removed) + new key 7
    for (int i = 0; i < 100; ++i) hashmap_set(&m, 5, i);     // churn an EXISTING key -> count unchanged
    CHECK(m.count == 4 && *hashmap_get(&m, 5) == 99);
    hashmap_free(&m);
}

TEST(containers, pool_lifo_reuse) {
    TestArena ta; Allocator al = ta.al;
    Pool<Obj> p; pool_init(&p, al, 4);
    uint32_t i0 = pool_alloc(&p), i1 = pool_alloc(&p), i2 = pool_alloc(&p), i3 = pool_alloc(&p);
    CHECK(i0 == 0 && i1 == 1 && i2 == 2 && i3 == 3);
    pool_free(&p, i1); pool_free(&p, i3); pool_free(&p, i0);   // free 1,3,0
    CHECK(pool_alloc(&p) == 0);                                // LIFO: last freed first
    CHECK(pool_alloc(&p) == 3);
    CHECK(pool_alloc(&p) == 1);
    CHECK(p.count == 4 && pool_alloc(&p) == POOL_INVALID);
    pool_free_all(&p);
    CHECK(pool_alloc(&p) == POOL_INVALID);                    // post free_all (cap 0)
}

TEST(containers, handlepool_forged_and_reuse) {
    TestArena ta; Allocator al = ta.al;
    HandlePool<Obj> hp; handlepool_init(&hp, al, 4);
    CHECK(handlepool_get(&hp, handle_make(2u, 1u)) == nullptr);   // never-allocated slot rejected (gen 0)
    CHECK(!handlepool_valid(&hp, handle_make(2u, 1u)));
    CHECK(!handlepool_valid(&hp, handle_make(20u, 1u)));          // out of range
    uint32_t cb = hp.count, fb = hp.free_head;
    CHECK(!handlepool_free(&hp, handle_make(20u, 1u)));
    CHECK(hp.count == cb && hp.free_head == fb);                  // rejected free changed nothing
    Obj o{}; Handle h[4];
    for (int i = 0; i < 4; ++i) { h[i] = handlepool_alloc(&hp, o); CHECK(handle_gen(h[i]) == 1u); }
    CHECK(handle_index(h[0]) != handle_index(h[1]));
    CHECK(handle_is_null(handlepool_alloc(&hp, o)));             // full
    for (int i = 0; i < 4; ++i) CHECK(handlepool_free(&hp, h[i]));
    Handle h2[4]; for (int i = 0; i < 4; ++i) h2[i] = handlepool_alloc(&hp, o);
    for (int i = 0; i < 4; ++i) {
        CHECK(handlepool_get(&hp, h[i]) == nullptr);            // old handles stale
        CHECK(handlepool_get(&hp, h2[i]) != nullptr);
        CHECK(handle_gen(h2[i]) == 2u);                          // each slot reused once
    }
    handlepool_free_all(&hp);
}

TEST(containers, corrupt_pool_allocation_is_mutation_free) {
    CHECK(pool_alloc<Obj>(nullptr) == POOL_INVALID);

    Obj slots[2]{{11u, 12u}, {21u, 22u}};
    Obj before[2]{};
    Pool<Obj> p{};
    p.slots = slots; p.cap = 2u; p.count = 0u; p.free_head = 2u;
    memcpy(before, slots, sizeof(slots));
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 0u && p.free_head == 2u && memcmp(before, slots, sizeof(slots)) == 0);

    p.free_head = 0u; p.count = 2u;
    memcpy(before, slots, sizeof(slots));
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 2u && p.free_head == 0u && memcmp(before, slots, sizeof(slots)) == 0);

    p.count = 3u;
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 3u && p.free_head == 0u);

    p.count = 0u; p.free_head = POOL_INVALID;
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 0u && p.free_head == POOL_INVALID);

    p.free_head = 0u; set_free_link(&slots[0], 2u);
    memcpy(before, slots, sizeof(slots));
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 0u && p.free_head == 0u && memcmp(before, slots, sizeof(slots)) == 0);

    p.slots = nullptr; p.count = 0u; p.free_head = 0u;
    CHECK(pool_alloc(&p) == POOL_INVALID);
    CHECK(p.count == 0u && p.free_head == 0u);
}

TEST(containers, corrupt_handlepool_allocation_is_mutation_free) {
    Obj value{99u, 100u};
    CHECK(handle_is_null(handlepool_alloc<Obj>(nullptr, value)));

    Obj slots[2]{{11u, 12u}, {21u, 22u}};
    Obj before[2]{};
    uint32_t generations[2]{0u, 0u};
    uint32_t generations_before[2]{};
    HandlePool<Obj> hp{};
    hp.slots = slots; hp.gen = generations; hp.cap = 2u; hp.count = 0u; hp.free_head = 2u;
    memcpy(before, slots, sizeof(slots));
    memcpy(generations_before, generations, sizeof(generations));
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 0u && hp.free_head == 2u && memcmp(before, slots, sizeof(slots)) == 0);
    CHECK(memcmp(generations_before, generations, sizeof(generations)) == 0);

    hp.free_head = 0u; hp.count = 2u;
    memcpy(before, slots, sizeof(slots));
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 2u && hp.free_head == 0u && memcmp(before, slots, sizeof(slots)) == 0);

    hp.count = 3u;
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 3u && hp.free_head == 0u);

    hp.count = 0u; hp.free_head = POOL_INVALID;
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 0u && hp.free_head == POOL_INVALID);

    hp.free_head = 0u; set_free_link(&slots[0], 2u);
    memcpy(before, slots, sizeof(slots));
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 0u && hp.free_head == 0u && memcmp(before, slots, sizeof(slots)) == 0);

    set_free_link(&slots[0], POOL_INVALID); generations[0] = HANDLE_GEN_MASK + 1u;
    memcpy(before, slots, sizeof(slots));
    memcpy(generations_before, generations, sizeof(generations));
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 0u && hp.free_head == 0u && memcmp(before, slots, sizeof(slots)) == 0);
    CHECK(memcmp(generations_before, generations, sizeof(generations)) == 0);

    hp.gen = nullptr;
    CHECK(handle_is_null(handlepool_alloc(&hp, value)));
    CHECK(hp.count == 0u && hp.free_head == 0u);
}

TEST(containers, array_str_edge_cases) {
    TestArena ta; Allocator al = ta.al;
    Array<int> a; array_init(&a, al); array_reserve(&a, 1); CHECK(a.cap == 8);   // geometric floor
    Array<int> b; array_init(&b, al); array_reserve(&b, 100); CHECK(b.cap >= 100);
    Array<int> c; array_init(&c, al); array_push(&c, 1); array_push(&c, 2); array_push(&c, 3);
    array_remove_swap(&c, 2);                                    // remove last index (self-assign, safe)
    CHECK(c.len == 2 && c.data[0] == 1 && c.data[1] == 2);
    array_clear(&c); array_push(&c, 99); CHECK(c.len == 1 && c.data[0] == 99);   // cap retained, append at 0
    array_free(&a); array_free(&b); array_free(&c);

    Str s; str_init(&s, al);
    str_append(&s, strview_cstr(""));    CHECK(s.len == 0);      // empty append
    str_append(&s, strview_cstr("12345678901234567890"));        // 20 -> crosses 16->32
    str_append(&s, strview_cstr("ABCDEFGHIJ"));                  // 30
    CHECK(s.len == 30 && s.cap >= 30);
    CHECK(strview_eq(str_view(&s), strview_cstr("12345678901234567890ABCDEFGHIJ")));
    Str t; str_init(&t, al); str_set(&t, strview_cstr("xyz")); str_set(&t, strview_cstr("")); CHECK(t.len == 0);
    Str e1; str_init(&e1, al); Str e2; str_init(&e2, al); CHECK(str_eq(&e1, &e2));   // two empty
    str_free(&s); str_free(&t); str_free(&e1); str_free(&e2);
}
