#pragma once

#include <stddef.h>
#include <stdint.h>

// Stable asset identity (ADR-0010). Runtime paths are canonicalized before the
// FNV-1a/64 hash is computed; generated constants arrive with the M4.1 cooker.
typedef uint64_t AssetId;

#define ASSET_ID_NULL ((AssetId)0)
#define ASSET_PATH_MAX 512u

constexpr AssetId ASSET_FNV1A_OFFSET = 14695981039346656037ULL;
constexpr AssetId ASSET_FNV1A_PRIME  = 1099511628211ULL;

constexpr bool asset_portable_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.';
}

constexpr bool asset_reserved_device_segment(const char* path, size_t begin, size_t end) {
    size_t stem_end = begin;
    while (stem_end < end && path[stem_end] != '.') ++stem_end;
    const size_t stem_length = stem_end - begin;
    if (stem_length == 3u) {
        return (path[begin] == 'c' && path[begin + 1u] == 'o' && path[begin + 2u] == 'n') ||
               (path[begin] == 'p' && path[begin + 1u] == 'r' && path[begin + 2u] == 'n') ||
               (path[begin] == 'a' && path[begin + 1u] == 'u' && path[begin + 2u] == 'x') ||
               (path[begin] == 'n' && path[begin + 1u] == 'u' && path[begin + 2u] == 'l');
    }
    if (stem_length == 4u) {
        const char digit = path[begin + 3u];
        const bool numbered = digit >= '1' && digit <= '9';
        return numbered &&
               ((path[begin] == 'c' && path[begin + 1u] == 'o' && path[begin + 2u] == 'm') ||
                (path[begin] == 'l' && path[begin + 1u] == 'p' && path[begin + 2u] == 't'));
    }
    return false;
}

constexpr bool asset_literal_segment_valid(const char* path, size_t begin, size_t end) {
    if (begin == end) return false;
    if (end - begin == 1u && path[begin] == '.') return false;
    if (end - begin == 2u && path[begin] == '.' && path[begin + 1u] == '.') return false;
    if (path[end - 1u] == '.' || asset_reserved_device_segment(path, begin, end)) return false;
    for (size_t i = begin; i < end; ++i)
        if (!asset_portable_char((unsigned char)path[i])) return false;
    return true;
}

constexpr AssetId asset_id_normalized_bytes(const char* path, size_t length) {
    if (!path || length == 0u || path[0] == '/' || path[length - 1u] == '/') return ASSET_ID_NULL;

    AssetId hash = ASSET_FNV1A_OFFSET;
    size_t segment_begin = 0u;
    for (size_t i = 0u; i < length; ++i) {
        const unsigned char c = (unsigned char)path[i];
        if (c == '/') {
            if (!asset_literal_segment_valid(path, segment_begin, i)) return ASSET_ID_NULL;
            segment_begin = i + 1u;
        } else if (!asset_portable_char(c)) {
            return ASSET_ID_NULL;
        }
        hash ^= c;
        hash *= ASSET_FNV1A_PRIME;
    }
    if (!asset_literal_segment_valid(path, segment_begin, length) || hash == ASSET_ID_NULL)
        return ASSET_ID_NULL;
    return hash;
}

// Compile-time quick-iteration form from ADR-0010. Literals must already be in
// canonical form (lowercase, forward slashes, no dot segments); invalid literals
// produce the reserved null id.
template<size_t N>
constexpr AssetId asset_id(const char (&path)[N]) {
    return N > 1u ? asset_id_normalized_bytes(path, N - 1u) : ASSET_ID_NULL;
}

// Canonical policy: relative path only; ASCII case-folded to lowercase; '\\' becomes
// '/'; repeated separators and '.' segments are removed. Segments use portable
// [a-z0-9._-] bytes and may not end in '.' or name a Windows device. Traversal,
// drive/absolute paths, aliases, non-ASCII bytes, and truncation are rejected.
// Failure leaves every output untouched.
bool asset_path_normalize(const char* path, char* out, size_t out_capacity,
                          size_t* out_length);
bool asset_id_from_path(const char* path, AssetId* out_id);
