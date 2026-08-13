#include "test.h"
#include "render/renderer.h"
#include "renderer_null_test.h"

#include <cstdint>

TEST(render_null, typed_handles_submit_and_deferred_destroy) {
    Renderer* renderer = renderer_create(nullptr);
    CHECK(renderer != nullptr);

    const float vertices[15] = {0,0,0,0,0, 1,0,0,1,0, 0,1,0,0,1};
    const uint16_t indices[3] = {0,1,2};
    MeshDesc mesh_desc{};
    mesh_desc.vertices = vertices;
    mesh_desc.vertex_count = 3;
    mesh_desc.vertex_stride = sizeof(float) * 5;
    mesh_desc.vertex_layout = RENDERER_VERTEX_POS3_UV2;
    mesh_desc.indices = indices;
    mesh_desc.index_count = 3;
    mesh_desc.index_type = RENDERER_INDEX_U16;
    MeshHandle mesh = renderer_create_mesh(renderer, &mesh_desc);

    const uint32_t pixel = 0xffffffffu;
    TextureDesc texture_desc{&pixel, 1, 1, RENDERER_TEXTURE_RGBA8_SRGB};
    TextureHandle texture = renderer_create_texture(renderer, &texture_desc);
    MaterialDesc material_desc{texture, RENDERER_SAMPLER_LINEAR_REPEAT};
    MaterialHandle material = renderer_create_material(renderer, &material_desc);
    CHECK(!handle_is_null(mesh.h));
    CHECK(!handle_is_null(texture.h));
    CHECK(!handle_is_null(material.h));

    FrameView view{};
    view.view = mm::mat4_identity();
    view.proj = mm::mat4_identity();
    DrawItem item{};
    item.model = mm::mat4_identity();
    item.mesh = mesh;
    item.material = material;
    CHECK(renderer_begin_frame(renderer, &view, 1280, 720, false));
    CHECK(renderer_submit(renderer, &item, 1));
    renderer_end_frame(renderer);
    RendererStats stats = renderer_get_stats(renderer);
    CHECK(stats.submitted_objects == 1);
    CHECK(stats.total_draw_calls == 0);

    renderer_destroy_material(renderer, material, 0);
    CHECK(renderer_begin_frame(renderer, &view, 1280, 720, false));
    CHECK(!renderer_submit(renderer, &item, 1));
    renderer_end_frame(renderer);
    renderer_destroy_texture(renderer, texture, 0);
    renderer_destroy_mesh(renderer, mesh, 0);
    renderer_destroy(renderer);
}

TEST(render_null, draw_capacity_rejection_precedes_item_reads_and_mutation) {
    Renderer* renderer = renderer_create(nullptr);
    CHECK(renderer != nullptr);

    const float vertices[15] = {0,0,0,0,0, 1,0,0,1,0, 0,1,0,0,1};
    const uint16_t indices[3] = {0,1,2};
    MeshDesc mesh_desc{};
    mesh_desc.vertices = vertices;
    mesh_desc.vertex_count = 3;
    mesh_desc.vertex_stride = sizeof(float) * 5;
    mesh_desc.vertex_layout = RENDERER_VERTEX_POS3_UV2;
    mesh_desc.indices = indices;
    mesh_desc.index_count = 3;
    mesh_desc.index_type = RENDERER_INDEX_U16;
    const MeshHandle mesh = renderer_create_mesh(renderer, &mesh_desc);

    const uint32_t pixel = 0xffffffffu;
    const TextureDesc texture_desc{&pixel, 1, 1, RENDERER_TEXTURE_RGBA8_SRGB};
    const TextureHandle texture = renderer_create_texture(renderer, &texture_desc);
    const MaterialDesc material_desc{texture, RENDERER_SAMPLER_LINEAR_REPEAT};
    const MaterialHandle material = renderer_create_material(renderer, &material_desc);
    CHECK(!handle_is_null(mesh.h));
    CHECK(!handle_is_null(texture.h));
    CHECK(!handle_is_null(material.h));

    FrameView view{};
    view.view = mm::mat4_identity();
    view.proj = mm::mat4_identity();
    DrawItem item{};
    item.model = mm::mat4_identity();
    item.mesh = mesh;
    item.material = material;
    CHECK(renderer_begin_frame(renderer, &view, 1280, 720, false));

    const uint32_t capacity = renderer_null_test_draw_capacity();
    const DrawItem* unreadable =
        reinterpret_cast<const DrawItem*>(static_cast<uintptr_t>(1u));
    CHECK(capacity == 4096u);
    CHECK(renderer_null_test_draw_count(renderer) == 0u);
    CHECK(renderer_submit(renderer, &item, 1u));
    CHECK(renderer_null_test_draw_count(renderer) == 1u);

    CHECK(renderer_null_test_set_draw_count(renderer, capacity - 1u));
    CHECK(renderer_submit(renderer, &item, 1u));
    CHECK(renderer_null_test_draw_count(renderer) == capacity);
    CHECK(!renderer_submit(renderer, unreadable, 1u));
    CHECK(renderer_null_test_draw_count(renderer) == capacity);

    CHECK(renderer_null_test_set_draw_count(renderer, 0u));
    CHECK(!renderer_submit(renderer, unreadable, capacity + 1u));
    CHECK(renderer_null_test_draw_count(renderer) == 0u);

    CHECK(renderer_null_test_set_draw_count(renderer, capacity + 1u));
    CHECK(!renderer_submit(renderer, unreadable, 1u));
    CHECK(renderer_null_test_draw_count(renderer) == capacity + 1u);

    CHECK(renderer_null_test_set_draw_count(renderer, UINT32_MAX));
    CHECK(!renderer_submit(renderer, unreadable, 1u));
    CHECK(renderer_null_test_draw_count(renderer) == UINT32_MAX);

    CHECK(renderer_null_test_set_draw_count(renderer, 0u));
    renderer_end_frame(renderer);
    CHECK(renderer_get_stats(renderer).submitted_objects == 0u);
    renderer_destroy(renderer);
}
