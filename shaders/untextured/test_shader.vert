#version 450

layout(location = 0) in vec3 vert_pos;
layout(location = 2) in vec3 vert_norm;

layout(std140, set = 1, binding = 0) uniform matrices {
    mat4 model;
    mat4 view;
    mat4 projection;
} mats;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;

void main() {
    gl_Position = mats.projection * mats.view * mats.model * vec4(vert_pos, 1.0);
    FragPos = vec3(mats.model * vec4(vert_pos, 1.0));
    Normal = vert_norm;
}
