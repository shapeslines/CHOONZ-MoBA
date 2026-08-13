// M2.1 render tests — the Vulkan-free pipeline-cache blob checker. Crafted byte
// blobs stand in for vkGetPipelineCacheData output; the real header layout is the
// spec's VkPipelineCacheHeaderVersionOne (see pipeline_cache_check.h).
#include "test.h"
#include "render/pipeline_cache_check.h"
#include "render_batch.h"
#include "render_debug_draw.h"
#include "render_handle_table.h"
#include <cstdint>
#include <cstring>

static const uint32_t VENDOR = 0x10DEu;       // NVIDIA, as on the dev box
static const uint32_t DEVICE = 0x1B81u;       // GTX 1070
static const uint8_t  UUID[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static void put_u32le(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// A valid 32-byte v1 header for VENDOR/DEVICE/UUID, plus `extra` payload bytes.
static size_t make_blob(uint8_t* out, size_t extra) {
    put_u32le(out + 0, PIPELINE_CACHE_HEADER_SIZE);
    put_u32le(out + 4, 1u);                   // VK_PIPELINE_CACHE_HEADER_VERSION_ONE
    put_u32le(out + 8, VENDOR);
    put_u32le(out + 12, DEVICE);
    std::memcpy(out + 16, UUID, 16);
    for (size_t i = 0; i < extra; ++i) out[PIPELINE_CACHE_HEADER_SIZE + i] = (uint8_t)i;
    return PIPELINE_CACHE_HEADER_SIZE + extra;
}

TEST(render, cache_blob_valid_header_accepted) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 64);
    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    // Header-only (no payload) is still structurally valid.
    CHECK(pipeline_cache_blob_ok(blob, PIPELINE_CACHE_HEADER_SIZE, VENDOR, DEVICE, UUID));
}

TEST(render, cache_blob_too_short_rejected) {
    uint8_t blob[256];
    make_blob(blob, 0);
    CHECK(!pipeline_cache_blob_ok(blob, PIPELINE_CACHE_HEADER_SIZE - 1, VENDOR, DEVICE, UUID));
    CHECK(!pipeline_cache_blob_ok(blob, 0, VENDOR, DEVICE, UUID));
    CHECK(!pipeline_cache_blob_ok(nullptr, 64, VENDOR, DEVICE, UUID));
}

TEST(render, cache_blob_bad_header_fields_rejected) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 16);

    put_u32le(blob + 0, 28u);                              // wrong headerSize
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    put_u32le(blob + 0, PIPELINE_CACHE_HEADER_SIZE);

    put_u32le(blob + 4, 2u);                               // unknown headerVersion
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    put_u32le(blob + 4, 1u);

    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));   // restored -> valid again
}

TEST(render, cache_blob_foreign_device_rejected) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 16);
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR + 1, DEVICE, UUID));   // other vendor
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE + 1, UUID));   // other GPU

    uint8_t other_uuid[16];
    std::memcpy(other_uuid, UUID, 16);
    other_uuid[15] ^= 0xFF;                                // driver update -> new UUID
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, other_uuid));
}

TEST(render, cache_blob_unaligned_source_ok) {
    // Disk bytes arrive with no alignment guarantee — the checker must memcpy-read.
    uint8_t backing[256 + 1];
    uint8_t* blob = backing + 1;                           // deliberately misaligned
    size_t n = make_blob(blob, 8);
    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
}

// G29: content-level defense — every one of the 32 header bytes is load-bearing, so
// flipping any single one must reject the blob. Payload bytes are deliberately NOT
// validated (the GPU validates payload data; the header is the trust boundary).
TEST(render, cache_blob_every_header_byte_is_load_bearing) {
    for (int byte = 0; byte < (int)PIPELINE_CACHE_HEADER_SIZE; ++byte) {
        uint8_t blob[256];
        size_t n = make_blob(blob, 64);
        blob[byte] ^= 0x01;
        CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    }

    uint8_t blob[256];
    size_t n = make_blob(blob, 64);
    put_u32le(blob + 0, PIPELINE_CACHE_HEADER_SIZE + 8u);   // headerSize overclaims
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));

    uint8_t partial[20];                                    // truncated mid-header
    std::memcpy(partial, blob, sizeof(partial));
    CHECK(!pipeline_cache_blob_ok(partial, 19, VENDOR, DEVICE, UUID));

    uint8_t garbage[PIPELINE_CACHE_HEADER_SIZE];            // fully random header
    for (size_t i = 0; i < sizeof(garbage); ++i) garbage[i] = (uint8_t)(i * 31u);
    CHECK(!pipeline_cache_blob_ok(garbage, sizeof(garbage), VENDOR, DEVICE, UUID));
}

static bool resolve_test_draw(void*, const DrawItem* item, uint32_t* out_pipeline_key) {
    if (!item || !out_pipeline_key || handle_is_null(item->mesh.h) || handle_is_null(item->material.h))
        return false;
    *out_pipeline_key = handle_index(item->material.h) & 1u;
    return true;
}

TEST(render, five_hundred_objects_coalesce_to_one_batch) {
    uint8_t memory[256 * 1024];
    Arena arena;
    arena_init_fixed(&arena, memory, sizeof(memory));

    DrawItem items[500]{};
    const MeshHandle mesh{handle_make(3, 1)};
    const MaterialHandle material{handle_make(7, 1)};
    for (uint32_t i = 0; i < 500; ++i) {
        items[i].mesh = mesh;
        items[i].material = material;
        items[i].model = mm::mat4_translate(mm::vec3_make((float)i, 0, 0));
    }

    RenderBatchOutput out{};
    CHECK(render_build_batches(items, 500, 4096, &arena, resolve_test_draw, nullptr, &out));
    CHECK(out.instance_count == 500);
    CHECK(out.batch_count == 1);
    CHECK(out.batches[0].instance_base == 0);
    CHECK(out.batches[0].instance_count == 500);
    CHECK_APPROX(out.instances[499].model.m[3][0], 499.0f, 0.001f);
}

TEST(render, batching_key_is_deterministic_and_material_aware) {
    uint8_t memory[64 * 1024];
    Arena arena;
    arena_init_fixed(&arena, memory, sizeof(memory));

    const MeshHandle mesh_a{handle_make(2, 1)};
    const MeshHandle mesh_b{handle_make(5, 1)};
    const MaterialHandle material_a{handle_make(4, 1)}; // pipeline key 0
    const MaterialHandle material_b{handle_make(7, 1)}; // pipeline key 1
    DrawItem items[4]{};
    items[0].model = mm::mat4_translate(mm::vec3_make(30, 0, 0)); items[0].mesh = mesh_b; items[0].material = material_b;
    items[1].model = mm::mat4_translate(mm::vec3_make(10, 0, 0)); items[1].mesh = mesh_a; items[1].material = material_a;
    items[2].model = mm::mat4_translate(mm::vec3_make(20, 0, 0)); items[2].mesh = mesh_a; items[2].material = material_a;
    items[3].model = mm::mat4_translate(mm::vec3_make(40, 0, 0)); items[3].mesh = mesh_b; items[3].material = material_a;

    RenderBatchOutput out{};
    CHECK(render_build_batches(items, 4, 4, &arena, resolve_test_draw, nullptr, &out));
    CHECK(out.batch_count == 3);
    CHECK(out.batches[0].pipeline_key == 0);
    CHECK(out.batches[0].mesh.h == mesh_a.h);
    CHECK(out.batches[0].instance_count == 2);
    CHECK_APPROX(out.instances[0].model.m[3][0], 10.0f, 0.001f);
    CHECK_APPROX(out.instances[1].model.m[3][0], 20.0f, 0.001f);
    CHECK(out.batches[2].pipeline_key == 1);
    CHECK(!render_build_batches(items, 4, 3, &arena, resolve_test_draw, nullptr, &out));
}

static void count_release(void* user, uint32_t) {
    ++*(uint32_t*)user;
}

TEST(render, handle_table_defers_reuse_and_rejects_stale_handles) {
    uint8_t memory[4096];
    Arena arena;
    arena_init_fixed(&arena, memory, sizeof(memory));
    RenderHandleTable table{};
    CHECK(render_handle_table_init(&table, &arena, 2));

    Handle first = render_handle_alloc(&table);
    Handle second = render_handle_alloc(&table);
    CHECK(render_handle_valid(&table, first));
    CHECK(render_handle_valid(&table, second));
    CHECK(handle_is_null(render_handle_alloc(&table)));

    CHECK(render_handle_retire(&table, first, 5));
    CHECK(!render_handle_valid(&table, first));
    uint32_t releases = 0;
    render_handle_collect(&table, 4, count_release, &releases);
    CHECK(releases == 0);
    CHECK(handle_is_null(render_handle_alloc(&table)));

    render_handle_collect(&table, 5, count_release, &releases);
    CHECK(releases == 1);
    Handle replacement = render_handle_alloc(&table);
    CHECK(handle_index(replacement) == handle_index(first));
    CHECK(handle_gen(replacement) != handle_gen(first));
    CHECK(!render_handle_valid(&table, first));
    CHECK(render_handle_valid(&table, replacement));
}

TEST(render, debug_draw_uses_one_frame_arena_block_and_resets) {
    alignas(16) uint8_t memory[64 * 1024]{};
    Arena arena;
    arena_init_fixed(&arena, memory, sizeof(memory));
    RenderDebugList list{};
    CHECK(render_debug_begin(&list, &arena, 2048));
    const size_t allocated_once = arena.offset;
    CHECK(render_debug_line(&list, mm::vec3_make(0,0,0), mm::vec3_make(1,0,0), 0xffffffffu));
    CHECK(render_debug_aabb(&list, mm::vec3_make(-1,-1,-1), mm::vec3_make(1,1,1), 0xff00ffffu));
    CHECK(render_debug_sphere(&list, mm::vec3_make(0,0,0), 1.0f, 0xffff00ffu, 8));
    render_debug_end_world(&list);
    CHECK(list.world_count == 2 + 24 + 48);
    CHECK(render_debug_text_2d(&list, 8.0f, 8.0f, 1.0f, 0xffffffffu, "FPS: 60"));
    CHECK(list.count > list.world_count);
    CHECK(arena.offset == allocated_once);
    arena_reset(&arena);
    CHECK(render_debug_begin(&list, &arena, 2048));
    CHECK(list.count == 0);
}
