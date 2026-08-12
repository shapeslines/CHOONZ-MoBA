#include "test.h"
#include "render/renderer.h"

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
