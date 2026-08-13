#pragma once

#include <stdint.h>

struct Renderer;

// Private acceptance seam for injecting otherwise unreachable corrupt submission state.
// This header is not part of eng_render_null's public include surface.
uint32_t renderer_null_test_draw_capacity(void);
uint32_t renderer_null_test_draw_count(const Renderer* renderer);
bool renderer_null_test_set_draw_count(Renderer* renderer, uint32_t count);
