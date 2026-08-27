#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"

typedef struct TgaImage {
    uint32_t width;
    uint32_t height;
    uint8_t* rgba8;       // width*height*4, rows top-down, caller allocated
} TgaImage;

// M4.0 direct runtime subset: true-color type 2 (raw) or 10 (RLE), 24/32 bpp,
// top/bottom-left origin, no palette/interleave/right-origin. Inspection validates
// the complete packet stream before any decode allocation. Failures leave outputs
// and the caller allocator untouched.
bool tga_inspect(const void* bytes, size_t size, uint32_t* out_width,
                 uint32_t* out_height, size_t* out_rgba8_bytes);
bool tga_decode(const void* bytes, size_t size, Allocator alloc, TgaImage* out);
