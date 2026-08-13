#include "assets/asset_id.h"

#include <string.h>

bool asset_path_normalize(const char* path, char* out, size_t out_capacity,
                          size_t* out_length) {
    if (!path || !out || !out_length || out_capacity == 0u || path[0] == '\0' ||
        path[0] == '/' || path[0] == '\\') {
        return false;
    }

    char normalized[ASSET_PATH_MAX];
    size_t write = 0u;
    size_t read = 0u;
    while (path[read] != '\0') {
        while (path[read] == '/' || path[read] == '\\') ++read;
        if (path[read] == '\0') break;

        const size_t segment_begin = read;
        while (path[read] != '\0' && path[read] != '/' && path[read] != '\\') ++read;
        const size_t segment_length = read - segment_begin;
        if (segment_length == 1u && path[segment_begin] == '.') continue;
        if (segment_length == 2u && path[segment_begin] == '.' &&
            path[segment_begin + 1u] == '.') {
            return false;
        }
        if (segment_length == 0u) continue;
        const size_t output_segment_begin = write != 0u ? write + 1u : write;
        if (write != 0u) {
            if (write + 1u >= sizeof(normalized)) return false;
            normalized[write++] = '/';
        }
        if (segment_length >= sizeof(normalized) - write) return false;
        for (size_t i = 0u; i < segment_length; ++i) {
            unsigned char c = (unsigned char)path[segment_begin + i];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
            if (!asset_portable_char(c)) return false;
            normalized[write++] = (char)c;
        }
        if (!asset_literal_segment_valid(normalized, output_segment_begin, write)) return false;
    }

    if (write == 0u || write + 1u > out_capacity) return false;
    normalized[write] = '\0';
    memcpy(out, normalized, write + 1u);
    *out_length = write;
    return true;
}

bool asset_id_from_path(const char* path, AssetId* out_id) {
    if (!out_id) return false;
    char normalized[ASSET_PATH_MAX];
    size_t length = 0u;
    if (!asset_path_normalize(path, normalized, sizeof(normalized), &length)) return false;
    const AssetId id = asset_id_normalized_bytes(normalized, length);
    if (id == ASSET_ID_NULL) return false;
    *out_id = id;
    return true;
}
