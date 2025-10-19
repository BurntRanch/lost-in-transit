#ifndef MODEL_H
#define MODEL_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <cglm/types.h>
#include <stdbool.h>
#include <stddef.h>

enum Shaders {
    UNTEXTURED_TEST_SHADER,
    TEXTURED_TEST_SHADER,
};

struct GraphicsPipeline {
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    SDL_GPUGraphicsPipeline *graphics_pipeline;
};

struct Vertex {
    vec3 vert;
    vec2 uv;
    vec3 norm;

    /* these should be seen as lists. only up to 4 bones can influence a single vertex. */
    ivec4 bone_ids;
    vec4 weights;
};

/* a texture used in a shader */
struct Texture {
    struct SDL_GPUSampler *gpu_sampler;
    struct SDL_GPUTexture *gpu_texture;
};

/* a mesh material, padded for std140 alignment compliance. */
struct Material {
    vec3 diffuse;
    float pad1;
    vec3 specular;
    float pad2;
    vec3 ambient;

    float shininess;
};

struct Buffer {
    struct SDL_GPUBuffer *buffer;

    /* amount of elements */
    size_t count;
};

struct Mesh {
    struct GraphicsPipeline *pipeline;

    struct Texture texture;

    struct Material material;

    struct Buffer vertex_buffer;
    struct Buffer index_buffer;
};

/* An object in a Model. */
struct Object {
    char *name;

    vec3 position;
    vec4 rotation;
    vec3 scale;

    struct Object *parent;

    struct Mesh *meshes;
    size_t mesh_count;

    /* don't use this, this is not reliable and only used internally for animations. */
    mat4 _transformation;
};

/* an animated vec3 value */
struct Vec3Keyframe {
    vec3 value;
    double timestamp;
};

/* an animated quaternion, usually used for rotations. */
struct QuatKeyframe {
    vec4 value;
    double timestamp;
};

/* Represents a bone in a Scene3D, the name corresponds to an object.
 * Bones need not to be animated. */
struct Bone {
    char *name;

    struct Vec3Keyframe *position_keys;
    size_t position_key_count;

    struct QuatKeyframe *rotation_keys;
    size_t rotation_key_count;

    struct Vec3Keyframe *scale_keys;
    size_t scale_key_count;

    mat4 offset_matrix;
    mat4 offset_matrix_inv;

    mat4 local_transform;
};

/* Simple metadata about an animation */
struct Animation {
    double duration;
    double ticks_per_sec;
};

struct Model {
    struct Bone bones[100];
    size_t bone_count;

    bool animation_playing;
    struct Animation current_animation;
    /* starts from 0 until the end of the animation */
    double animation_time;

    /* An array of objects
     * Objects are guaranteed to be stored before their children (if any). */
    struct Object *objects;
    size_t object_count;
};

/* a light in the scene, padded for std140 alignment compliance. */
struct Light {
    vec3 pos;
    float pad1;

    vec3 diffuse;
    float pad2;
    
    vec3 specular;
    float pad3;

    vec3 ambient;
    float pad4;

    Uint64 scene_ptr;
};

struct LightUBO {
    int lights_count; // 4 bytes
    char pad1[12];  // 12 + 4 bytes

    struct Light lights[256]; // starts at 16 bytes
};

extern struct LightUBO MLLightUBO;

/* Imports a GLTF 2.0 file as a Model.
 * filename isn't sanitized
 * use MLDestroyModel to destroy. */
struct Model *MLImportModel(const char * const filename);

/* returns index to pModel->bones, returns -1 on fail (wraps around to size_t max) */
size_t MLFindBoneByName(const struct Model *pModel, const char *name);

void MLDestroyModel(struct Model *pModel);
#endif
