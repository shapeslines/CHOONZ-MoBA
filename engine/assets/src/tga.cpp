#include "assets/tga.h"

#include <string.h>

typedef struct TgaInfo {
    const uint8_t* pixels;
    size_t pixel_bytes_available;
    size_t pixel_count;
    uint32_t width;
    uint32_t height;
    uint8_t bytes_per_pixel;
    uint8_t image_type;
    uint8_t top_left;
} TgaInfo;

static uint16_t read_u16le(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static bool inspect_tga(const void* bytes, size_t size, TgaInfo* out) {
    if (!bytes || !out || size < 18u) return false;
    const uint8_t* data = (const uint8_t*)bytes;
    const uint8_t id_length = data[0];
    const uint8_t color_map_type = data[1];
    const uint8_t image_type = data[2];
    const uint16_t width = read_u16le(data + 12u);
    const uint16_t height = read_u16le(data + 14u);
    const uint8_t bits_per_pixel = data[16];
    const uint8_t descriptor = data[17];

    if (color_map_type != 0u || (image_type != 2u && image_type != 10u) ||
        (bits_per_pixel != 24u && bits_per_pixel != 32u) ||
        width == 0u || height == 0u || (descriptor & 0x10u) != 0u ||
        (descriptor & 0xC0u) != 0u) return false;
    const uint8_t attribute_bits = descriptor & 0x0fu;
    if ((bits_per_pixel == 24u && attribute_bits != 0u) ||
        (bits_per_pixel == 32u && attribute_bits != 0u && attribute_bits != 8u))
        return false;

    const size_t data_offset = 18u + (size_t)id_length;
    if (data_offset > size) return false;
    const size_t bytes_per_pixel = bits_per_pixel / 8u;
    const size_t pixel_count = (size_t)width * (size_t)height;
    const size_t available = size - data_offset;

    if (image_type == 2u) {
        if (pixel_count > SIZE_MAX / bytes_per_pixel ||
            available < pixel_count * bytes_per_pixel) return false;
    } else {
        size_t cursor = 0u;
        size_t produced = 0u;
        while (produced < pixel_count) {
            if (cursor >= available) return false;
            const uint8_t packet = data[data_offset + cursor++];
            const size_t run = (size_t)(packet & 0x7fu) + 1u;
            if (run > pixel_count - produced) return false;
            const size_t source_pixels = (packet & 0x80u) != 0u ? 1u : run;
            if (source_pixels > SIZE_MAX / bytes_per_pixel) return false;
            const size_t source_bytes = source_pixels * bytes_per_pixel;
            if (source_bytes > available - cursor) return false;
            cursor += source_bytes;
            produced += run;
        }
    }

    TgaInfo info{};
    info.pixels = data + data_offset;
    info.pixel_bytes_available = available;
    info.pixel_count = pixel_count;
    info.width = width;
    info.height = height;
    info.bytes_per_pixel = (uint8_t)bytes_per_pixel;
    info.image_type = image_type;
    info.top_left = (descriptor & 0x20u) != 0u ? 1u : 0u;
    *out = info;
    return true;
}

bool tga_inspect(const void* bytes, size_t size, uint32_t* out_width,
                 uint32_t* out_height, size_t* out_rgba8_bytes) {
    if (!out_width || !out_height || !out_rgba8_bytes) return false;
    TgaInfo info{};
    if (!inspect_tga(bytes, size, &info) || info.pixel_count > SIZE_MAX / 4u)
        return false;
    *out_width = info.width;
    *out_height = info.height;
    *out_rgba8_bytes = info.pixel_count * 4u;
    return true;
}

static void write_pixel(const TgaInfo* info, uint8_t* destination,
                        size_t file_pixel_index, const uint8_t* source) {
    const size_t file_y = file_pixel_index / info->width;
    const size_t x = file_pixel_index - file_y * info->width;
    const size_t output_y = info->top_left ? file_y : (size_t)info->height - 1u - file_y;
    uint8_t* pixel = destination + (output_y * info->width + x) * 4u;
    pixel[0] = source[2];
    pixel[1] = source[1];
    pixel[2] = source[0];
    pixel[3] = info->bytes_per_pixel == 4u ? source[3] : (uint8_t)255u;
}

bool tga_decode(const void* bytes, size_t size, Allocator alloc, TgaImage* out) {
    if (!out || !alloc.fn) return false;
    TgaInfo info{};
    if (!inspect_tga(bytes, size, &info) || info.pixel_count > SIZE_MAX / 4u)
        return false;
    const size_t output_bytes = info.pixel_count * 4u;
    uint8_t* rgba8 = (uint8_t*)mem_alloc(alloc, output_bytes, MEM_DEFAULT_ALIGN);
    if (!rgba8) return false;

    if (info.image_type == 2u) {
        for (size_t i = 0u; i < info.pixel_count; ++i)
            write_pixel(&info, rgba8, i, info.pixels + i * info.bytes_per_pixel);
    } else {
        size_t cursor = 0u;
        size_t produced = 0u;
        while (produced < info.pixel_count) {
            const uint8_t packet = info.pixels[cursor++];
            const size_t run = (size_t)(packet & 0x7fu) + 1u;
            if ((packet & 0x80u) != 0u) {
                const uint8_t* source = info.pixels + cursor;
                cursor += info.bytes_per_pixel;
                for (size_t i = 0u; i < run; ++i)
                    write_pixel(&info, rgba8, produced++, source);
            } else {
                for (size_t i = 0u; i < run; ++i) {
                    write_pixel(&info, rgba8, produced++, info.pixels + cursor);
                    cursor += info.bytes_per_pixel;
                }
            }
        }
    }

    TgaImage image{};
    image.width = info.width;
    image.height = info.height;
    image.rgba8 = rgba8;
    *out = image;
    return true;
}
