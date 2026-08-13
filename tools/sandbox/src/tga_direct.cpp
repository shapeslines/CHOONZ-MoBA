#include "tga_direct.h"
#include <string.h>

// TGA header is 18 bytes, little-endian, unaligned — memcpy-read everything.
static uint16_t read_u16le(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

bool tga_decode(const void* bytes, size_t size, Allocator alloc, TgaImage* out) {
    if (!bytes || !out || !alloc.fn || size < 18) return false;
    const uint8_t* p = (const uint8_t*)bytes;

    const uint8_t  id_length  = p[0];
    const uint8_t  cmap_type  = p[1];
    const uint8_t  image_type = p[2];
    const uint16_t width      = read_u16le(p + 12);
    const uint16_t height     = read_u16le(p + 14);
    const uint8_t  bpp        = p[16];
    const uint8_t  descriptor = p[17];

    if (cmap_type != 0) return false;            // palettes: M4.0's problem
    if (image_type != 2) return false;           // only uncompressed true-color
    if (bpp != 24 && bpp != 32) return false;
    if (width == 0 || height == 0) return false;
    if (descriptor & 0x10) return false;         // right-to-left origin: never seen, reject
    if (descriptor & 0xC0) return false;         // interleave (deprecated): would decode scrambled, reject

    const bool   top_left  = (descriptor & 0x20) != 0;   // bit 5; default TGA is bottom-left
    const size_t px_bytes  = (size_t)bpp / 8;
    const size_t px_count  = (size_t)width * height;
    const size_t data_off  = 18u + id_length;
    // Truncation check in u64 so 0xFFFF x 0xFFFF headers can't wrap a 32-bit size_t.
    // This rejects BEFORE the allocation, so a tiny file with huge claimed dims can
    // never push gigabytes into the caller's arena (hard-abort policy, see platform.h).
    const uint64_t need = (uint64_t)px_count * px_bytes;
    if (size < data_off || (uint64_t)(size - data_off) < need) return false;      // truncated

    uint8_t* dst = (uint8_t*)mem_alloc(alloc, px_count * 4, MEM_DEFAULT_ALIGN);
    if (!dst) return false;

    const uint8_t* src = p + data_off;
    for (size_t y = 0; y < height; ++y) {
        // File rows run from the origin; we always emit top-down.
        const size_t src_y = top_left ? y : (size_t)(height - 1) - y;
        const uint8_t* s = src + src_y * width * px_bytes;
        uint8_t*       d = dst + y * width * 4;
        for (size_t x = 0; x < width; ++x) {     // file pixels are BGR(A)
            d[x * 4 + 0] = s[x * px_bytes + 2];
            d[x * 4 + 1] = s[x * px_bytes + 1];
            d[x * 4 + 2] = s[x * px_bytes + 0];
            d[x * 4 + 3] = px_bytes == 4 ? s[x * px_bytes + 3] : (uint8_t)255;
        }
    }
    out->width  = width;
    out->height = height;
    out->rgba8  = dst;
    return true;
}
