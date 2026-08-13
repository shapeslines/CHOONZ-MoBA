#pragma once
#include <stdint.h>
#include <string.h>          // memcpy, memmove, strlen, memcmp
#include "core/mem.h"
#include "core/assert.h"
// Length-prefixed strings (ARCHITECTURE §6.4). NOT null-terminated (a NUL is only
// added at OS/Vulkan boundaries). StrView is a non-owning slice; Str owns an
// allocator-backed buffer. C-style, global.

struct StrView { const char* data; uint32_t len; };
struct Str     { char* data; uint32_t len; uint32_t cap; Allocator alloc; };

// ---- StrView (non-owning) ----
static inline StrView strview(const char* s, uint32_t n) { StrView v; v.data = s; v.len = n; return v; }
static inline StrView strview_cstr(const char* s) { StrView v; v.data = s; v.len = (uint32_t)strlen(s); return v; }
static inline bool    strview_eq(StrView a, StrView b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

// ---- Str (owning) ----
inline void str_init(Str* s, Allocator al) { s->data = nullptr; s->len = 0; s->cap = 0; s->alloc = al; }
inline void str_free(Str* s) {
    if (s->data) mem_free(s->alloc, s->data, s->cap);
    s->data = nullptr; s->len = 0; s->cap = 0;
}
static inline bool str_alias_offset(const Str* s, StrView v, uint32_t* out_offset) {
    if (!s || !s->data || !v.data || !out_offset) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(s->data);
    if (static_cast<uintptr_t>(s->cap) > UINTPTR_MAX - base) return false;
    const uintptr_t end = base + static_cast<uintptr_t>(s->cap);
    const uintptr_t source = reinterpret_cast<uintptr_t>(v.data);
    if (source < base || source > end || static_cast<uintptr_t>(v.len) > end - source)
        return false;
    *out_offset = static_cast<uint32_t>(source - base);
    return true;
}
inline void str_reserve(Str* s, uint32_t n) {
    if (n <= s->cap) return;
    uint64_t nc64 = s->cap ? (uint64_t)s->cap * 2u : 16u;
    if (nc64 < n) nc64 = n;
    ENSURE_MSG(nc64 <= 0xFFFFFFFFu, "Str capacity overflow");
    uint32_t nc = (uint32_t)nc64;
    char* nd = (char*)mem_alloc(s->alloc, nc, 1);
    ENSURE_MSG(nd != nullptr, "str allocation failed");
    if (s->data) { memcpy(nd, s->data, s->len); mem_free(s->alloc, s->data, s->cap); }
    s->data = nd; s->cap = nc;
}
inline void str_set(Str* s, StrView v) {
    uint32_t alias_offset = 0u;
    const bool source_aliases = str_alias_offset(s, v, &alias_offset);
    str_reserve(s, v.len);
    if (source_aliases) {
        v.data = s->data + alias_offset;
        if (v.len) memmove(s->data, v.data, v.len);
    } else if (v.len) {
        memcpy(s->data, v.data, v.len);
    }
    s->len = v.len;
}
inline void str_append(Str* s, StrView v) {
    uint32_t alias_offset = 0u;
    const bool source_aliases = str_alias_offset(s, v, &alias_offset);
    uint64_t need = (uint64_t)s->len + v.len;            // compute in 64-bit: u32 add could wrap and skip growth
    ENSURE_MSG(need <= 0xFFFFFFFFu, "Str length overflow");
    str_reserve(s, (uint32_t)need);
    if (source_aliases) {
        v.data = s->data + alias_offset;
        if (v.len) memmove(s->data + s->len, v.data, v.len);
    } else if (v.len) {
        memcpy(s->data + s->len, v.data, v.len);
    }
    s->len = (uint32_t)need;
}
inline StrView str_view(const Str* s) { return strview(s->data, s->len); }
inline bool    str_eq(const Str* a, const Str* b) { return strview_eq(str_view(a), str_view(b)); }
