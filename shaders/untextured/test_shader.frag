#version 450

struct Light {
    vec3 pos;

    vec3 diffuse;
    vec3 specular;
    vec3 ambient;
};

layout(std140, set = 3, binding = 0) uniform material {
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;
} mat;

layout(std140, set = 3, binding = 1) uniform lights_array {
    Light lights[256];
    int lights_count;
} lights;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 result = mat.ambient;

    for (int i = 0; i < lights.lights_count; i++) {
        result += (lights.lights[i].ambient * (1.0 / lights.lights_count));
    }

    outColor = vec4(result, 1.0);
}
