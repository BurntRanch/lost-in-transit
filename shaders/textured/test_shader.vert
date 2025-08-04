#version 450

layout(location = 0) in vec3 vert_pos;
layout(location = 1) in vec2 vert_uv;

layout(std140, set = 1, binding = 0) uniform matrices {
    mat4 model;
    mat4 view;
    mat4 projection;
} mats;

layout(location = 0) out vec2 uv;

void main() {
    uv = vert_uv;

    gl_Position = mats.projection * mats.view * mats.model * vec4(vert_pos, 1.0);
}
