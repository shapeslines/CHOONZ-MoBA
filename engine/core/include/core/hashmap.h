#pragma once
#include <stdint.h>
#include <stddef.h>
#include <type_traits>       // is_trivially_copyable
#include "core/mem.h"
#include "core/assert.h"
#include "core/str.h"        // StrView key support
// HashMap<K,V> — open-addressing, Robin Hood, power-of-two capacity, backward-shift
// delete (no tombstones) (ARCHITECTURE §6.4). Allocator-aware, no exceptions.
//
// !! Iteration order is NOT deterministic -> NEVER iterate a HashMap from the sim;
//    use an Array for ordered sim data. NO iteration API is provided, by design.
//    (Hash maps are presentation/tools only.)
//
// Lifetime contracts:
//   - A returned V* (and Array T*) is INVALIDATED by any insert that grows the map
//     (rehash). Don't hold it across mutations; use a Handle for a durable reference.
//   - HashMap<StrView,V> keys are NON-OWNING: the key bytes must outlive the entry.
//     Intern transient keys into stable storage before insert (a scratch-arena key
//     dangles after the arena resets).
//
// Keys need hashmap_hash(K) + hashmap_eq(K,K) overloads (provided for integers + StrView).

inline uint64_t hashmap_hash(uint32_t k) { uint64_t x = k; x *= 0x9E3779B97F4A7C15ULL; x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ULL; x ^= x >> 32; return x; }
inline uint64_t hashmap_hash(int32_t  k) { return hashmap_hash((uint32_t)k); }
inline uint64_t hashmap_hash(uint64_t k) { k ^= k >> 30; k *= 0xBF58476D1CE4E5B9ULL; k ^= k >> 27; k *= 0x94D049BB133111EBULL; k ^= k >> 31; return k; }
inline uint64_t hashmap_hash(StrView s)  { uint64_t h = 14695981039346656037ULL; for (uint32_t i = 0; i < s.len; ++i) { h ^= (uint8_t)s.data[i]; h *= 1099511628211ULL; } return h; }
inline bool hashmap_eq(uint32_t a, uint32_t b) { return a == b; }
inline bool hashmap_eq(int32_t  a, int32_t  b) { return a == b; }
inline bool hashmap_eq(uint64_t a, uint64_t b) { return a == b; }
inline bool hashmap_eq(StrView  a, StrView  b) { return strview_eq(a, b); }

template<class K, class V>
struct HashMap {
    struct Slot { K key; V val; uint64_t hash; uint8_t occupied; };
    Slot*     slots;
    uint32_t  cap;        // power of two (or 0)
    uint32_t  mask;       // cap - 1
    uint32_t  count;
    Allocator alloc;
};

template<class K, class V> inline void hashmap_init(HashMap<K,V>* m, Allocator al) {
    static_assert(std::is_trivially_copyable<K>::value && std::is_trivially_copyable<V>::value,
                  "HashMap<K,V> stores K/V in raw slots; both must be trivially copyable");
    m->slots = nullptr; m->cap = 0; m->mask = 0; m->count = 0; m->alloc = al;
}
template<class K, class V> inline void hashmap_free(HashMap<K,V>* m) {
    if (m->slots) mem_free(m->alloc, m->slots, (size_t)m->cap * sizeof(typename HashMap<K,V>::Slot));
    m->slots = nullptr; m->cap = 0; m->mask = 0; m->count = 0;
}
template<class K, class V> inline void hashmap_clear(HashMap<K,V>* m) {
    for (uint32_t i = 0; i < m->cap; ++i) m->slots[i].occupied = 0;
    m->count = 0;
}

// Robin-Hood insert of an element with a precomputed hash; updates value if key exists.
template<class K, class V> inline void hashmap__place(HashMap<K,V>* m, K key, V val, uint64_t h) {
    uint32_t mask = m->mask, pos = (uint32_t)h & mask, dib = 0;
    for (;;) {
        typename HashMap<K,V>::Slot* s = &m->slots[pos];
        if (!s->occupied) { s->key = key; s->val = val; s->hash = h; s->occupied = 1; ++m->count; return; }
        if (s->hash == h && hashmap_eq(s->key, key)) { s->val = val; return; }   // update
        uint32_t edib = (pos - ((uint32_t)s->hash & mask)) & mask;
        if (edib < dib) {                                                        // rich yields to poor
            K tk = s->key; V tv = s->val; uint64_t th = s->hash;
            s->key = key; s->val = val; s->hash = h;
            key = tk; val = tv; h = th; dib = edib;
        }
        pos = (pos + 1) & mask; ++dib;
    }
}
template<class K, class V> inline void hashmap__rehash(HashMap<K,V>* m, uint32_t newcap) {
    typedef typename HashMap<K,V>::Slot Slot;
    Slot* old = m->slots; uint32_t oldcap = m->cap;
    Slot* ns = (Slot*)mem_alloc(m->alloc, (size_t)newcap * sizeof(Slot), alignof(Slot));
    ENSURE_MSG(ns != nullptr, "hashmap allocation failed");
    for (uint32_t i = 0; i < newcap; ++i) ns[i].occupied = 0;
    m->slots = ns; m->cap = newcap; m->mask = newcap - 1; m->count = 0;
    if (old) {
        for (uint32_t i = 0; i < oldcap; ++i) if (old[i].occupied) hashmap__place(m, old[i].key, old[i].val, old[i].hash);
        mem_free(m->alloc, old, (size_t)oldcap * sizeof(Slot));
    }
}
template<class K, class V> inline void hashmap_set(HashMap<K,V>* m, K key, V val) {
    if (m->cap == 0) hashmap__rehash(m, 16);
    else if (((size_t)m->count + 1) * 8 >= (size_t)m->cap * 7) {
        ENSURE_MSG(m->cap <= UINT32_MAX / 2u, "HashMap capacity overflow");
        hashmap__rehash(m, m->cap * 2);   // grow at 7/8
    }
    hashmap__place(m, key, val, hashmap_hash(key));
}
template<class K, class V> inline V* hashmap_get(HashMap<K,V>* m, K key) {
    if (m->cap == 0) return nullptr;
    uint64_t h = hashmap_hash(key); uint32_t mask = m->mask, pos = (uint32_t)h & mask, dib = 0;
    for (;;) {
        typename HashMap<K,V>::Slot* s = &m->slots[pos];
        if (!s->occupied) return nullptr;
        if (s->hash == h && hashmap_eq(s->key, key)) return &s->val;
        uint32_t edib = (pos - ((uint32_t)s->hash & mask)) & mask;
        if (edib < dib) return nullptr;                          // robin-hood early-out: would've stolen this slot
        pos = (pos + 1) & mask; ++dib;
    }
}
template<class K, class V> inline bool hashmap_has(HashMap<K,V>* m, K key) { return hashmap_get(m, key) != nullptr; }

template<class K, class V> inline bool hashmap_remove(HashMap<K,V>* m, K key) {
    if (m->cap == 0) return false;
    uint64_t h = hashmap_hash(key); uint32_t mask = m->mask, pos = (uint32_t)h & mask, dib = 0;
    for (;;) {                                                   // locate
        typename HashMap<K,V>::Slot* s = &m->slots[pos];
        if (!s->occupied) return false;
        if (s->hash == h && hashmap_eq(s->key, key)) break;
        uint32_t edib = (pos - ((uint32_t)s->hash & mask)) & mask;
        if (edib < dib) return false;
        pos = (pos + 1) & mask; ++dib;
    }
    for (;;) {                                                   // backward-shift
        uint32_t next = (pos + 1) & mask;
        typename HashMap<K,V>::Slot* sn = &m->slots[next];
        if (!sn->occupied) break;
        uint32_t ndib = (next - ((uint32_t)sn->hash & mask)) & mask;
        if (ndib == 0) break;                                    // sn is at its home -> stop
        m->slots[pos] = *sn; pos = next;
    }
    m->slots[pos].occupied = 0; --m->count;
    return true;
}
