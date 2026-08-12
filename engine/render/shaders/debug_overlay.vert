#version 450

layout(push_constant) uniform OverlayPush {
    vec2 viewport_size;
} overlay;
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    // Positive Vulkan viewport height maps NDC -1 to the top edge.
    vec2 ndc = vec2(in_position.x * 2.0 / overlay.viewport_size.x - 1.0,
                    in_position.y * 2.0 / overlay.viewport_size.y - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    out_color = in_color;
}
