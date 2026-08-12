#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 0, binding = 1, std430) readonly buffer InstanceBuffer {
    mat4 model[];
} instances;

layout(push_constant) uniform DrawPush {
    uint instance_base;
} draw_push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 out_uv;

void main() {
    mat4 model = instances.model[gl_InstanceIndex + draw_push.instance_base];
    gl_Position = camera.proj * camera.view * model * vec4(in_position, 1.0);
    out_uv = in_uv;
}
