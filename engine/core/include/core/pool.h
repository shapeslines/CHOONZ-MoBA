#pragma once
#include <stdint.h>
#include <string.h>          // memcpy
#include <type_traits>       // is_trivially_copyable
#include "core/mem.h"
#include "core/handle.h"
#include "core/assert.h"
// Fixed-capacity object pools backed by an INTRUSIVE free list: a free slot stores
// the next-free index inside its own memory (zero overhead; requires sizeof(T) >= 4).
//   Pool<T>       — index-addressed slots (caller manages identity).
//   HandlePool<T> — Pool + per-slot generation -> hands out generational Handles
//                   (ADR-0003); stale handles fail validation, never crash.
// Fixed capacity (allocated once); growable pools are deferred.
// Pool has NO double-free protection (debug ASSERT only) -> prefer HandlePool when
// liveness is uncertain. HandlePool rejects double-free / stale handles by generation.

#define POOL_INVALID 0xFFFFFFFFu

// ---------------- Pool<T> ----------------
template<class T> struct Pool {
    T*        slots;
    uint32_t  cap;
    uint32_t  free_head;
    uint32_t  count;
    Allocator alloc;
};
template<class T> inline void pool_init(Pool<T>* p, Allocator al, uint32_t cap) {
    static_assert(sizeof(T) >= sizeof(uint32_t), "Pool<T> needs sizeof(T) >= 4 (intrusive free list)");
    static_assert(std::is_trivially_copyable<T>::value, "Pool<T> stores plain slots; T must be trivially copyable");
    p->cap = cap; p->count = 0; p->alloc = al; p->free_head = POOL_INVALID; p->slots = nullptr;
    if (cap == 0) return;                              // empty pool is well-defined for any allocator
    p->slots = (T*)mem_alloc(al, (size_t)cap * sizeof(T), alignof(T));
    ENSURE_MSG(p->slots != nullptr, "pool allocation failed");
    for (uint32_t i = 0; i < cap; ++i) { uint32_t nxt = (i + 1u < cap) ? i + 1u : POOL_INVALID; memcpy(&p->slots[i], &nxt, 4); }
    p->free_head = 0u;
}
template<class T> inline void pool_free_all(Pool<T>* p) {
    if (p->slots) mem_free(p->alloc, p->slots, (size_t)p->cap * sizeof(T));
    p->slots = nullptr; p->cap = 0; p->count = 0; p->free_head = POOL_INVALID;
}
template<class T> inline uint32_t pool_alloc(Pool<T>* p) {       // -> index or POOL_INVALID
    if (!p || !p->slots || p->cap == 0 || p->count >= p->cap ||
        p->free_head == POOL_INVALID || p->free_head >= p->cap) return POOL_INVALID;
    uint32_t idx = p->free_head;
    uint32_t nxt; memcpy(&nxt, &p->slots[idx], 4);
    if (nxt != POOL_INVALID && nxt >= p->cap) return POOL_INVALID;
    p->free_head = nxt; ++p->count;
    return idx;
}
template<class T> inline void pool_free(Pool<T>* p, uint32_t idx) {
    ASSERT(idx < p->cap);
    ASSERT(p->count > 0);                 // debug: catch double-free / free-of-empty (no full protection — use HandlePool)
    uint32_t nxt = p->free_head; memcpy(&p->slots[idx], &nxt, 4);
    p->free_head = idx; --p->count;
}
template<class T> inline T* pool_at(Pool<T>* p, uint32_t idx) { ASSERT(idx < p->cap); return &p->slots[idx]; }

// ---------------- HandlePool<T> ----------------
template<class T> struct HandlePool {
    T*        slots;
    uint32_t* gen;          // current generation per slot (live slot's gen matches its handle; never 0)
    uint32_t  cap;
    uint32_t  free_head;
    uint32_t  count;
    Allocator alloc;
};
template<class T> inline void handlepool_init(HandlePool<T>* hp, Allocator al, uint32_t cap) {
    static_assert(sizeof(T) >= sizeof(uint32_t), "HandlePool<T> needs sizeof(T) >= 4 (intrusive free list)");
    static_assert(std::is_trivially_copyable<T>::value, "HandlePool<T> stores plain slots; T must be trivially copyable");
    ENSURE_MSG(cap <= (HANDLE_INDEX_MASK + 1u), "HandlePool capacity exceeds the handle index space");
    hp->cap = cap; hp->count = 0; hp->alloc = al; hp->free_head = POOL_INVALID; hp->slots = nullptr; hp->gen = nullptr;
    if (cap == 0) return;
    hp->slots = (T*)mem_alloc(al, (size_t)cap * sizeof(T), alignof(T));
    hp->gen   = (uint32_t*)mem_alloc(al, (size_t)cap * sizeof(uint32_t), alignof(uint32_t));
    ENSURE_MSG(hp->slots && hp->gen, "handlepool allocation failed");
    for (uint32_t i = 0; i < cap; ++i) {
        hp->gen[i] = 0u;                                          // 0 = never allocated -> rejects forged handles
        uint32_t nxt = (i + 1u < cap) ? i + 1u : POOL_INVALID; memcpy(&hp->slots[i], &nxt, 4);
    }
    hp->free_head = 0u;
}
template<class T> inline void handlepool_free_all(HandlePool<T>* hp) {
    if (hp->slots) mem_free(hp->alloc, hp->slots, (size_t)hp->cap * sizeof(T));
    if (hp->gen)   mem_free(hp->alloc, hp->gen,   (size_t)hp->cap * sizeof(uint32_t));
    hp->slots = nullptr; hp->gen = nullptr; hp->cap = 0; hp->count = 0; hp->free_head = POOL_INVALID;
}
template<class T> inline Handle handlepool_alloc(HandlePool<T>* hp, T value) {
    if (!hp || !hp->slots || !hp->gen || hp->cap == 0 || hp->count >= hp->cap ||
        hp->free_head == POOL_INVALID || hp->free_head >= hp->cap) return HANDLE_NULL; // full or corrupt
    uint32_t idx = hp->free_head;
    uint32_t nxt; memcpy(&nxt, &hp->slots[idx], 4);
    if (nxt != POOL_INVALID && nxt >= hp->cap) return HANDLE_NULL;
    hp->free_head = nxt;
    if (hp->gen[idx] == 0u) hp->gen[idx] = 1u;                    // first use of a never-allocated slot (gen 0 -> 1)
    hp->slots[idx] = value; ++hp->count;
    return handle_make(idx, hp->gen[idx]);
}
template<class T> inline bool handlepool_valid(const HandlePool<T>* hp, Handle h) {
    uint32_t idx = handle_index(h);
    uint32_t g   = handle_gen(h);
    return g != 0u && idx < hp->cap && hp->gen[idx] == g;
}
template<class T> inline T* handlepool_get(HandlePool<T>* hp, Handle h) {
    uint32_t idx = handle_index(h);
    uint32_t g   = handle_gen(h);
    if (g == 0u || idx >= hp->cap || hp->gen[idx] != g) return nullptr;   // null or stale -> null, never crash
    return &hp->slots[idx];
}
template<class T> inline bool handlepool_free(HandlePool<T>* hp, Handle h) {
    if (!handlepool_valid(hp, h)) return false;
    uint32_t idx = handle_index(h);
    uint32_t g = hp->gen[idx] + 1u;
    ASSERT(g <= HANDLE_GEN_MASK);                                 // debug: catch the 16,383rd-reuse wrap (ADR-0003)
    if (g > HANDLE_GEN_MASK) g = 1u;                              // release: wrap safely past 0 (accepted aliasing window)
    hp->gen[idx] = g;
    uint32_t nxt = hp->free_head; memcpy(&hp->slots[idx], &nxt, 4);
    hp->free_head = idx; --hp->count;
    return true;
}
