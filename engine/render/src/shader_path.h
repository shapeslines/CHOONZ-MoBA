#pragma once
#include <stddef.h>

// Compose one offline-SPIR-V path. False means invalid input, formatting failure,
// or truncation; callers must return before attempting platform file access.
bool render_shader_path(char* out, size_t capacity,
                        const char* shader_dir, const char* shader_name);
