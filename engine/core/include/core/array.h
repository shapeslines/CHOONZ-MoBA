#pragma once
#include <stdint.h>
#include <string.h>          // memcpy
#include <type_traits>       // alignof helpers
#include "core/mem.h"
#include "core/assert.h"
// Array<T> — dynamic, geometric growth, allocator-aware, POD-friendly. Public
// data/len/cap (data-oriented; no getters). No exceptions, no global allocation
// (ARCHITECTURE §6.4). T must be trivially relocatable (DOD value types).

template<class T>
struct Array {
    T*        data;
    uint32_t  len;
    uint32_t  cap;
    Allocator alloc;
};

template<class T> inline void array_init(Array<T>* a, Allocator al) {
    static_assert(std::is_trivially_copyable<T>::value, "Array<T> relocates via memcpy; T must be trivially copyable");
    a->data = nullptr; a->len = 0; a->cap = 0; a->alloc = al;
}
template<class T> inline void array_free(Array<T>* a) {
    if (a->data) mem_free(a->alloc, a->data, (size_t)a->cap * sizeof(T));
    a->data = nullptr; a->len = 0; a->cap = 0;
}
template<class T> inline void array_reserve(Array<T>* a, uint32_t n) {
    if (n <= a->cap) return;
    uint64_t nc64 = a->cap ? (uint64_t)a->cap * 2u : 8u;       // 64-bit: u32 doubling can wrap past 0x7FFFFFFF
    if (nc64 < n) nc64 = n;
    ENSURE_MSG(nc64 <= 0xFFFFFFFFu, "Array capacity overflow");
    uint32_t nc = (uint32_t)nc64;
    T* nd = (T*)mem_alloc(a->alloc, (size_t)nc * sizeof(T), alignof(T));
    ENSURE_MSG(nd != nullptr, "array allocation failed");
    if (a->data) {
        memcpy(nd, a->data, (size_t)a->len * sizeof(T));
        mem_free(a->alloc, a->data, (size_t)a->cap * sizeof(T));
    }
    a->data = nd; a->cap = nc;
}
template<class T> inline T* array_push(Array<T>* a, T v) {
    ENSURE_MSG(a->len < 0xFFFFFFFFu, "Array length overflow");
    array_reserve(a, a->len + 1u);
    a->data[a->len] = v;
    return &a->data[a->len++];
}
template<class T> inline T array_pop(Array<T>* a) {
    ASSERT(a->len > 0);
    return a->data[--a->len];
}
// O(1) unordered removal: move the last element into the hole.
template<class T> inline void array_remove_swap(Array<T>* a, uint32_t i) {
    ASSERT(i < a->len);
    a->data[i] = a->data[--a->len];
}
template<class T> inline void array_clear(Array<T>* a) { a->len = 0; }
template<class T> inline bool array_empty(const Array<T>* a) { return a->len == 0; }
