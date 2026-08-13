#pragma once

#include <stdint.h>

// Shared by both renderer backends so corrupt current state is rejected before
// subtraction and before any caller item can be read.
static inline bool render_submission_preflight(const void* items, uint32_t count,
                                               uint32_t current, uint32_t capacity) {
    return current <= capacity && count <= capacity - current && (items || count == 0u);
}
