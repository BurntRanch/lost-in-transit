#version 450

layout(location = 0) in vec3 vert_pos;
layout(location = 1) in vec2 vert_uv;
layout(location = 2) in vec3 vert_norm;
layout(location = 3) in ivec4 bone_ids;
layout(location = 4) in vec4 weights;

layout(std140, set = 1, binding = 0) uniform matrices {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 bone_matrices[100];
} mats;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 uv;

void main() {
    mat4 bone_mat = mat4(0.0f);
    bool found_any = false;
    for (int i = 0; i < 4; i++) {
        if (bone_ids[i] < 0) {
            continue;
        }
        found_any = true;
        bone_mat += mats.bone_matrices[bone_ids[i]] * weights[i];
    }
    if (!found_any) {
        bone_mat = mat4(1.0f);
    }

    gl_Position = mats.projection * mats.view * mats.model * bone_mat * vec4(vert_pos, 1.0f);
    uv = vert_uv;
    FragPos = vec3(mats.model * vec4(vert_pos, 1.0f));
    Normal = vert_norm;
}
