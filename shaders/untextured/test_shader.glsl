#version 450
#define MAX_LIGHTS 256

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;

struct Light {
    vec3 pos;

    vec3 diffuse;
    vec3 specular;
    vec3 ambient;

    double _;
};

layout(std140, set = 3, binding = 0) uniform material_ubo {
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;

    float shininess;
} material;

layout(std140, set = 3, binding = 1) uniform lights_ubo {
    int light_count;
    Light lights[MAX_LIGHTS];
} lightsUBO;

layout(std140, set = 3, binding = 2) uniform camerainfo_ubo {
    vec3 pos;
} cameraInfo;

void CelShadingFrag(inout vec3 result, vec3 norm) {
    for (int i = 0; i < lightsUBO.light_count; i++) {
        Light light = lightsUBO.lights[i];

        vec3 viewDir = normalize(cameraInfo.pos - FragPos);
        vec3 lightDir = normalize(light.pos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);

        float diff = dot(norm, lightDir);
        float spec = dot(viewDir, reflectDir) > 0.98 ? 1.0 : 0.0;

        if (diff < 0.0) {
            diff = 0.0;
        } else if (diff < 0.25) {
            diff = 0.25;
        } else if (diff < 0.5) {
            diff = 0.5;
        } else if (diff < 0.75) {
            diff = 0.75;
        } else {
            diff = 1.0;
        }

        result += (light.ambient * material.ambient) + (diff * light.diffuse * material.diffuse) + (spec * light.specular * material.specular) * (1.0 / lightsUBO.light_count);
    }
}

layout(location = 0) out vec4 outColor;

void main() {
    vec3 result = vec3(0);

    CelShadingFrag(result, normalize(Normal));

    outColor = vec4(result, 1.0);
}
