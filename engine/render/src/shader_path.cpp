#include "shader_path.h"
#include <cstdio>

bool render_shader_path(char* out, size_t capacity,
                        const char* shader_dir, const char* shader_name) {
    if (!out || capacity == 0 || !shader_dir || !shader_name) return false;
    const int path_length = std::snprintf(out, capacity, "%s/%s", shader_dir, shader_name);
    return path_length >= 0 && (size_t)path_length < capacity;
}
