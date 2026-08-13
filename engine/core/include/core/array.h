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

inline size_t array__allocation_bytes(uint32_t count, size_t item_size) {
    ENSURE_MSG(item_size != 0 && (size_t)count <= SIZE_MAX / item_size,
               "Array allocation size overflow");
    return (size_t)count * item_size;
}

template<class T> inline void array_init(Array<T>* a, Allocator al) {
    static_assert(std::is_trivially_copyable<T>::value, "Array<T> relocates via memcpy; T must be trivially copyable");
    a->data = nullptr; a->len = 0; a->cap = 0; a->alloc = al;
}
template<class T> inline void array_free(Array<T>* a) {
    if (a->data) mem_free(a->alloc, a->data, array__allocation_bytes(a->cap, sizeof(T)));
    a->data = nullptr; a->len = 0; a->cap = 0;
}
template<class T> inline void array_reserve(Array<T>* a, uint32_t n) {
    ENSURE_MSG(a != nullptr, "Array is not initialized");
    ENSURE_MSG(a->len <= a->cap, "Array length exceeds capacity");
    if (n <= a->cap) return;
    uint64_t nc64 = a->cap ? (uint64_t)a->cap * 2u : 8u;       // 64-bit: u32 doubling can wrap past 0x7FFFFFFF
    if (nc64 < n) nc64 = n;
    ENSURE_MSG(nc64 <= UINT32_MAX, "Array capacity overflow");
    uint32_t nc = (uint32_t)nc64;
    size_t new_bytes = array__allocation_bytes(nc, sizeof(T));
    T* nd = (T*)mem_alloc(a->alloc, new_bytes, alignof(T));
    ENSURE_MSG(nd != nullptr, "array allocation failed");
    if (a->data) {
        memcpy(nd, a->data, array__allocation_bytes(a->len, sizeof(T)));
        mem_free(a->alloc, a->data, array__allocation_bytes(a->cap, sizeof(T)));
    }
    a->data = nd; a->cap = nc;
}
template<class T> inline T* array_push(Array<T>* a, T v) {
    ENSURE_MSG(a != nullptr, "Array is not initialized");
    ENSURE_MSG(a->len < UINT32_MAX, "Array length overflow");
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
