#version 450

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;

struct Light {
    vec3 pos;

    vec3 diffuse;
    vec3 specular;
    vec3 ambient;

    double _;
};

layout(std140, set = 3, binding = 0) uniform material {
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;

    float shininess;
} mat;

layout(std140, set = 3, binding = 1) uniform lights_array {
    int lights_count;
    Light lights[256];
} lights;

layout(std140, set = 3, binding = 2) uniform camera_info {
    vec3 pos;
} camera;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 result = vec3(0);

    vec3 norm = normalize(Normal);

    for (int i = 0; i < lights.lights_count; i++) {
        Light light = lights.lights[i];

        vec3 viewDir = normalize(camera.pos - FragPos);
        vec3 lightDir = normalize(light.pos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);

        float diff = max(dot(norm, lightDir), 0.0);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), mat.shininess);

        result += (light.ambient * mat.ambient) + (diff * light.diffuse * mat.diffuse) + (spec * light.specular * mat.specular) * (1.0 / lights.lights_count);
    }

    outColor = vec4(result, 1.0);
}
