#include "render_debug_draw.h"

#include <cmath>

static bool render_debug_has_room(const RenderDebugList* list, uint32_t vertices) {
    return list && list->count <= list->capacity && vertices <= list->capacity - list->count;
}

bool render_debug_begin(RenderDebugList* list, Arena* frame_arena, uint32_t capacity) {
    if (!list || !frame_arena || capacity == 0) return false;
    list->vertices = ARENA_PUSH_ARRAY(frame_arena, RenderDebugVertex, capacity);
    list->count = 0;
    list->capacity = capacity;
    list->world_count = 0;
    return list->vertices != nullptr;
}

bool render_debug_line(RenderDebugList* list, mm::vec3 a, mm::vec3 b, uint32_t color) {
    if (!render_debug_has_room(list, 2u)) return false;
    list->vertices[list->count++] = {a, color};
    list->vertices[list->count++] = {b, color};
    return true;
}

bool render_debug_aabb(RenderDebugList* list, mm::vec3 lo, mm::vec3 hi, uint32_t color) {
    const mm::vec3 p[8] = {
        mm::vec3_make(lo.x, lo.y, lo.z), mm::vec3_make(hi.x, lo.y, lo.z),
        mm::vec3_make(hi.x, hi.y, lo.z), mm::vec3_make(lo.x, hi.y, lo.z),
        mm::vec3_make(lo.x, lo.y, hi.z), mm::vec3_make(hi.x, lo.y, hi.z),
        mm::vec3_make(hi.x, hi.y, hi.z), mm::vec3_make(lo.x, hi.y, hi.z),
    };
    const uint8_t edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7},
    };
    if (!render_debug_has_room(list, 24u)) return false;
    for (uint32_t i = 0; i < 12; ++i)
        render_debug_line(list, p[edges[i][0]], p[edges[i][1]], color);
    return true;
}

bool render_debug_sphere(RenderDebugList* list, mm::vec3 center, float radius,
                         uint32_t color, uint32_t segments) {
    if (!list || radius <= 0.0f || segments < 3 || segments > UINT32_MAX / 6u ||
        !render_debug_has_room(list, segments * 6u))
        return false;
    const float tau = 6.28318530718f;
    for (uint32_t ring = 0; ring < 3; ++ring) {
        for (uint32_t i = 0; i < segments; ++i) {
            const float a = tau * (float)i / (float)segments;
            const float b = tau * (float)(i + 1) / (float)segments;
            mm::vec3 p0 = center, p1 = center;
            if (ring == 0) { p0.x += cosf(a)*radius; p0.z += sinf(a)*radius; p1.x += cosf(b)*radius; p1.z += sinf(b)*radius; }
            if (ring == 1) { p0.x += cosf(a)*radius; p0.y += sinf(a)*radius; p1.x += cosf(b)*radius; p1.y += sinf(b)*radius; }
            if (ring == 2) { p0.y += cosf(a)*radius; p0.z += sinf(a)*radius; p1.y += cosf(b)*radius; p1.z += sinf(b)*radius; }
            render_debug_line(list, p0, p1, color);
        }
    }
    return true;
}

void render_debug_end_world(RenderDebugList* list) {
    if (list) list->world_count = list->count;
}

static void glyph_columns(char c, uint8_t out[5]) {
    for (uint32_t i = 0; i < 5; ++i) out[i] = 0;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
        {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
    };
    if (c >= '0' && c <= '9') { for (uint32_t i=0;i<5;++i) out[i]=digits[c-'0'][i]; return; }
    if (c >= 'A' && c <= 'Z') { for (uint32_t i=0;i<5;++i) out[i]=letters[c-'A'][i]; return; }
    if (c == ':') { out[2] = 0x14; return; }
    if (c == '.') { out[2] = 0x40; return; }
    if (c == '-') { out[1]=out[2]=out[3]=0x08; }
}

bool render_debug_text_2d(RenderDebugList* list, float x, float y, float scale,
                          uint32_t color, const char* text) {
    if (!list || !text || scale <= 0.0f) return false;
    const float origin_x = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { x = origin_x; y += 9.0f * scale; continue; }
        uint8_t columns[5]; glyph_columns(*p, columns);
        for (uint32_t col = 0; col < 5; ++col) {
            for (uint32_t row = 0; row < 7; ++row) {
                if ((columns[col] & (1u << row)) == 0) continue;
                const mm::vec3 a = mm::vec3_make(x + col*scale, y + row*scale, 0.0f);
                const mm::vec3 b = mm::vec3_make(x + (col+0.8f)*scale, y + row*scale, 0.0f);
                if (!render_debug_line(list, a, b, color)) return false;
            }
        }
        x += 6.0f * scale;
    }
    return true;
}
