#version 450
// M2.2 textured quad, M2.3 camera: verts are WORLD-space (XY plane, z=0) around the
// origin; the per-frame view/proj UBO at set=0 (written by the renderer each frame)
// warps them into clip space. set=1 stays the material (texture+sampler).

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} cam;

layout(location = 0) in  vec2 a_pos;
layout(location = 1) in  vec2 a_uv;
layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = cam.proj * cam.view * vec4(a_pos, 0.0, 1.0);
    v_uv        = a_uv;
}
