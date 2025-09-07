#version 450

layout(location = 0) in vec3 vert_pos;
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

void main() {
    vec4 pos = vec4(0.0f);
    bool found_any = false;
    for (int i = 0; i < 4; i++) {
        if (bone_ids[i] == -1) {
            continue;
        }
        found_any = true;
        if (bone_ids[i] >= 100) {
            pos = vec4(vert_pos, 1.0f);
            break;
        }
        vec4 local_pos = mats.bone_matrices[bone_ids[i]] * vec4(vert_pos, 1.0f);
        pos += local_pos * weights[i];
    }
    if (!found_any) {
        pos = vec4(vert_pos, 1.0f);
    }

    mat4 viewModel = mats.view * mats.model;
    gl_Position = mats.projection * viewModel * pos;
    FragPos = vec3(mats.model * vec4(vert_pos, 1.0));
    Normal = vert_norm;
}
