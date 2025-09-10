#include "scenes/game/intro.h"
#include "assimp/anim.h"
#include "assimp/matrix4x4.h"
#include "assimp/quaternion.h"
#include "engine.h"
#include "networking.h"
#include "scenes.h"
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <assert.h>
#include <assimp/color4.h>
#include <assimp/defs.h>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/types.h>
#include <assimp/vector3.h>
#include <cglm/cam.h>
#include <cglm/cglm.h>
#include <cglm/affine.h>
#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <string.h>

enum Shaders {
    UNTEXTURED_TEST_SHADER,
    TEXTURED_TEST_SHADER,
};

struct Shader {
    SDL_GPUShader *vertex;
    SDL_GPUShader *fragment;
};

struct Vertex {
    vec3 vert;
    vec2 uv;
    vec3 norm;

    /* these should be seen as lists. only up to 4 bones can influence a single vertex. */
    ivec4 bone_ids;
    vec4 weights;
};

struct Buffer {
    struct SDL_GPUBuffer *buffer;

    /* amount of elements */
    size_t count;
};

struct Texture {
    struct SDL_GPUSampler *gpu_sampler;
    struct SDL_GPUTexture *gpu_texture;
};

struct Light {
    vec3 pos;
    float pad1;

    vec3 diffuse;
    float pad2;
    
    vec3 specular;
    float pad3;

    vec3 ambient;
    float pad4;
};

struct Material {
    vec3 diffuse;
    float pad1;
    vec3 specular;
    float pad2;
    vec3 ambient;

    float shininess;
};

struct Mesh {
    /* which shader should we use to render this? */
    enum Shaders shader;

    struct Texture texture;

    struct Material material;

    struct Buffer vertex_buffer;
    struct Buffer index_buffer;
};

struct Vec3Keyframe {
    struct aiVector3D value;
    double timestamp;
};

struct QuatKeyframe {
    struct aiQuaternion value;
    double timestamp;
};

struct Bone {
    char *name;

    struct Vec3Keyframe *position_keys;
    size_t position_key_count;

    struct QuatKeyframe *rotation_keys;
    size_t rotation_key_count;

    struct Vec3Keyframe *scale_keys;
    size_t scale_key_count;

    struct aiMatrix4x4 offset_matrix;
    struct aiMatrix4x4 local_transform;
};

struct Animation {
    double duration;
    double ticks_per_sec;
};

struct Scene {
    struct Bone bones[100];

    size_t bone_count;

    bool animation_playing;
    struct Animation current_animation;
    /* starts from 0 until the end of the animation */
    double animation_time;
};

/* An object in the game world */
struct Object {
    char *name;

    struct aiVector3D position;
    struct aiQuaternion rotation;
    struct aiVector3D scale;

    struct Mesh *meshes;
    size_t mesh_count;

    /* don't use this, this is not reliable and only used internally for animations. */
    struct aiMatrix4x4 transformation;

    struct Scene *scene;
    struct Object *parent;
};

struct PlayerObjectList {
    struct PlayerObjectList *prev;

    struct Object *obj;
    int id;

    struct PlayerObjectList *next;
};

static SDL_GPUDevice *gpu_device = NULL;

static struct Shader untextured_test_shader;
static struct Shader textured_test_shader;
static SDL_GPUGraphicsPipeline *untextured_test_pipeline = NULL;
static SDL_GPUGraphicsPipeline *textured_test_pipeline = NULL;

static struct Object **objects_array = NULL;
static Uint32 objects_count = 0;

static struct Scene *intro_scene = NULL;

static struct PlayerObjectList *player_objects;

static vec3 camera_pos = { 0, 0, 0 };
static float camera_pitch, camera_yaw = 0;

alignas(16) static struct MatricesUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 bone_matrices[100];
} matrices;

static struct LightsUBO {
    int lights_count; // 4 bytes
    char pad1[12];  // 12 + 4 bytes

    struct Light lights[256]; // starts at 16 bytes
} lights;

/* Don't laugh */
static inline bool LoadShader(const char *fileName, Uint8 **ppBufferOut, size_t *pSizeOut) {
    void *data = NULL;

    if (!(data = SDL_LoadFile(fileName, pSizeOut))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open shader file %s! (SDL Error: %s)\n", fileName, SDL_GetError());
        return false;
    }

    *ppBufferOut = data;

    return true;
}

static inline bool InitTexturedTestPipeline() {
    static SDL_GPUShaderCreateInfo vertex_shader_create_info, fragment_shader_create_info;

    vertex_shader_create_info.code = NULL;
    vertex_shader_create_info.code_size = sizeof(NULL);
    vertex_shader_create_info.entrypoint = "main";
    vertex_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_shader_create_info.num_samplers = 0;
    vertex_shader_create_info.num_storage_buffers = 0;
    vertex_shader_create_info.num_storage_textures = 0;
    vertex_shader_create_info.num_uniform_buffers = 1;
    vertex_shader_create_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_shader_create_info.props = 0;

    if (!LoadShader("shaders/textured/test_shader.vert.spv", (Uint8 **)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size)) {
        return false;
    }

    fragment_shader_create_info.code = NULL;
    fragment_shader_create_info.code_size = sizeof(NULL);
    fragment_shader_create_info.entrypoint = "main";
    fragment_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_shader_create_info.num_samplers = 1;
    fragment_shader_create_info.num_storage_buffers = 0;
    fragment_shader_create_info.num_storage_textures = 0;
    fragment_shader_create_info.num_uniform_buffers = 3;
    fragment_shader_create_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_shader_create_info.props = 0;

    if (!LoadShader("shaders/textured/test_shader.frag.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
        return false;
    }

    if (!(textured_test_shader.vertex = SDL_CreateGPUShader(gpu_device, &vertex_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test vertex GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(textured_test_shader.fragment = SDL_CreateGPUShader(gpu_device, &fragment_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test fragment GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_free((void *)vertex_shader_create_info.code);
    SDL_free((void *)fragment_shader_create_info.code);

    struct SDL_GPUColorTargetDescription color_target_description;
    color_target_description.blend_state.enable_color_write_mask = false;
    color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
    color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    color_target_description.blend_state.enable_blend = false;
    color_target_description.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    struct SDL_GPUVertexBufferDescription vertex_buffer_description;
    vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_description.instance_step_rate = 0;
    vertex_buffer_description.pitch = sizeof(struct Vertex);
    vertex_buffer_description.slot = 0;

    struct SDL_GPUVertexAttribute vertex_attributes[5];
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].location = 0;
    vertex_attributes[0].offset = 0;
    
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[1].location = 1;
    vertex_attributes[1].offset = offsetof(struct Vertex, uv);

    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[2].location = 2;
    vertex_attributes[2].offset = offsetof(struct Vertex, norm);
    
    vertex_attributes[3].buffer_slot = 0;
    vertex_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
    vertex_attributes[3].location = 3;
    vertex_attributes[3].offset = offsetof(struct Vertex, bone_ids);
    
    vertex_attributes[4].buffer_slot = 0;
    vertex_attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    vertex_attributes[4].location = 4;
    vertex_attributes[4].offset = offsetof(struct Vertex, weights);

    struct SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.target_info.has_depth_stencil_target = true;
    graphics_pipeline_create_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    graphics_pipeline_create_info.target_info.color_target_descriptions = &color_target_description;
    graphics_pipeline_create_info.target_info.num_color_targets = 1;
    graphics_pipeline_create_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.enable_stencil_test = false;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_test = true;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_write = true;
    graphics_pipeline_create_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    graphics_pipeline_create_info.multisample_state.sample_mask = 0;
    graphics_pipeline_create_info.multisample_state.enable_mask = false;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_clamp = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_constant_factor = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_slope_factor = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_bias = true;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_clip = false;
    graphics_pipeline_create_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    graphics_pipeline_create_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    graphics_pipeline_create_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_attributes = 5;
    graphics_pipeline_create_info.vertex_input_state.vertex_attributes = vertex_attributes;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_buffers = 1;
    graphics_pipeline_create_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
    graphics_pipeline_create_info.vertex_shader = textured_test_shader.vertex;
    graphics_pipeline_create_info.fragment_shader = textured_test_shader.fragment;
    graphics_pipeline_create_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    graphics_pipeline_create_info.props = 0;

    if (!(textured_test_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &graphics_pipeline_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test graphics pipeline! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}
static inline bool InitUntexturedTestPipeline() {
    static SDL_GPUShaderCreateInfo vertex_shader_create_info, fragment_shader_create_info;

    vertex_shader_create_info.code = NULL;
    vertex_shader_create_info.code_size = sizeof(NULL);
    vertex_shader_create_info.entrypoint = "main";
    vertex_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_shader_create_info.num_samplers = 0;
    vertex_shader_create_info.num_storage_buffers = 0;
    vertex_shader_create_info.num_storage_textures = 0;
    vertex_shader_create_info.num_uniform_buffers = 1;
    vertex_shader_create_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_shader_create_info.props = 0;

    if (!LoadShader("shaders/untextured/test_shader.vert.spv", (Uint8 **)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size)) {
        return false;
    }

    fragment_shader_create_info.code = NULL;
    fragment_shader_create_info.code_size = sizeof(NULL);
    fragment_shader_create_info.entrypoint = "main";
    fragment_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_shader_create_info.num_samplers = 0;
    fragment_shader_create_info.num_storage_buffers = 0;
    fragment_shader_create_info.num_storage_textures = 0;
    fragment_shader_create_info.num_uniform_buffers = 3;
    fragment_shader_create_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_shader_create_info.props = 0;

    if (!LoadShader("shaders/untextured/test_shader.frag.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
        return false;
    }

    if (!(untextured_test_shader.vertex = SDL_CreateGPUShader(gpu_device, &vertex_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test vertex GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(untextured_test_shader.fragment = SDL_CreateGPUShader(gpu_device, &fragment_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test fragment GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_free((void *)vertex_shader_create_info.code);
    SDL_free((void *)fragment_shader_create_info.code);

    struct SDL_GPUColorTargetDescription color_target_description;
    color_target_description.blend_state.enable_color_write_mask = false;
    color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
    color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    color_target_description.blend_state.enable_blend = false;
    color_target_description.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    struct SDL_GPUVertexBufferDescription vertex_buffer_description;
    vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_description.instance_step_rate = 0;
    vertex_buffer_description.pitch = sizeof(struct Vertex);
    vertex_buffer_description.slot = 0;

    struct SDL_GPUVertexAttribute vertex_attributes[4];
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].location = 0;
    vertex_attributes[0].offset = 0;

    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[1].location = 2;
    vertex_attributes[1].offset = offsetof(struct Vertex, norm);
    
    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
    vertex_attributes[2].location = 3;
    vertex_attributes[2].offset = offsetof(struct Vertex, bone_ids);
    
    vertex_attributes[3].buffer_slot = 0;
    vertex_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    vertex_attributes[3].location = 4;
    vertex_attributes[3].offset = offsetof(struct Vertex, weights);
    
    struct SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.target_info.has_depth_stencil_target = true;
    graphics_pipeline_create_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    graphics_pipeline_create_info.target_info.color_target_descriptions = &color_target_description;
    graphics_pipeline_create_info.target_info.num_color_targets = 1;
    graphics_pipeline_create_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.enable_stencil_test = false;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_test = true;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_write = true;
    graphics_pipeline_create_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    graphics_pipeline_create_info.multisample_state.sample_mask = 0;
    graphics_pipeline_create_info.multisample_state.enable_mask = false;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_clamp = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_constant_factor = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_slope_factor = 0.0f;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_bias = true;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_clip = false;
    graphics_pipeline_create_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    graphics_pipeline_create_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    graphics_pipeline_create_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_attributes = 4;
    graphics_pipeline_create_info.vertex_input_state.vertex_attributes = vertex_attributes;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_buffers = 1;
    graphics_pipeline_create_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
    graphics_pipeline_create_info.vertex_shader = untextured_test_shader.vertex;
    graphics_pipeline_create_info.fragment_shader = untextured_test_shader.fragment;
    graphics_pipeline_create_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    graphics_pipeline_create_info.props = 0;

    if (!(untextured_test_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &graphics_pipeline_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test graphics pipeline! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}
static inline bool CreateVertexBuffer(const struct Vertex *pVertices, size_t vertexCount, struct Buffer *pVertexBufferOut) {
    SDL_GPUCopyPass *copy_pass;

    if (!(copy_pass = SDL_BeginGPUCopyPass(LECommandBuffer))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin GPU copy pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_GPUBufferCreateInfo vertex_buffer_create_info;
    vertex_buffer_create_info.props = 0;
    vertex_buffer_create_info.size = sizeof(struct Vertex) * vertexCount;
    vertex_buffer_create_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

    if (!(pVertexBufferOut->buffer = SDL_CreateGPUBuffer(gpu_device, &vertex_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static SDL_GPUTransferBuffer *transfer_buffer = NULL;

    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = vertex_buffer_create_info.size;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    if (!(transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    void *data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);

    SDL_memcpy(data, pVertices, vertex_buffer_create_info.size);

    SDL_GPUTransferBufferLocation src;
    src.offset = 0;
    src.transfer_buffer = transfer_buffer;

    SDL_GPUBufferRegion dst;
    dst.offset = 0;
    dst.size = vertex_buffer_create_info.size;
    dst.buffer = pVertexBufferOut->buffer;

    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

    SDL_EndGPUCopyPass(copy_pass);

    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);

    pVertexBufferOut->count = vertexCount;

    return true;
}

static inline bool CreateIndexBuffer(const Sint32 *pIndices, size_t indexCount, struct Buffer *pIndexBufferOut) {
    SDL_GPUCopyPass *copy_pass;

    if (!(copy_pass = SDL_BeginGPUCopyPass(LECommandBuffer))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin GPU copy pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_GPUBufferCreateInfo index_buffer_create_info;
    index_buffer_create_info.props = 0;
    index_buffer_create_info.size = sizeof(Sint32) * indexCount;
    index_buffer_create_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;

    if (!(pIndexBufferOut->buffer = SDL_CreateGPUBuffer(gpu_device, &index_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static SDL_GPUTransferBuffer *transfer_buffer = NULL;

    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = index_buffer_create_info.size;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    if (!(transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    void *data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);

    SDL_memcpy(data, pIndices, index_buffer_create_info.size);

    SDL_GPUTransferBufferLocation src;
    src.offset = 0;
    src.transfer_buffer = transfer_buffer;

    SDL_GPUBufferRegion dst;
    dst.offset = 0;
    dst.size = index_buffer_create_info.size;
    dst.buffer = pIndexBufferOut->buffer;

    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

    SDL_EndGPUCopyPass(copy_pass);

    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);

    pIndexBufferOut->count = indexCount;

    return true;
}

static inline bool CopySurfaceToTexture(struct SDL_Surface *surface, struct SDL_GPUTexture *texture) {
    static SDL_GPUCopyPass *copy_pass;
    if (!(copy_pass = SDL_BeginGPUCopyPass(LECommandBuffer))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to begin GPU copy pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = surface->pitch * surface->h;

    static SDL_GPUTransferBuffer *transfer_buffer;
    if (!(transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    void *transfer_buffer_data;
    if (!(transfer_buffer_data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    SDL_memcpy(transfer_buffer_data, surface->pixels, transfer_buffer_create_info.size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    static SDL_GPUTextureTransferInfo source_transfer_info;
    source_transfer_info.offset = 0;
    source_transfer_info.pixels_per_row = surface->w;
    source_transfer_info.rows_per_layer = surface->h;
    source_transfer_info.transfer_buffer = transfer_buffer;

    static SDL_GPUTextureRegion dest_region;
    dest_region.x = 0;
    dest_region.y = 0;
    dest_region.z = 0;
    dest_region.w = surface->w;
    dest_region.h = surface->h;
    dest_region.d = 1;
    dest_region.layer = 0;
    dest_region.mip_level = 0;
    dest_region.texture = texture;

    SDL_UploadToGPUTexture(copy_pass, &source_transfer_info, &dest_region, false);

    SDL_EndGPUCopyPass(copy_pass);

    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);

    return true;
}

/* returns index to scene->bones, returns -1 on fail (wraps around to size_t max) */
static inline size_t FindBoneByName(const struct Scene *scene, const char *name) {
    for (size_t bone_idx = 0; bone_idx < scene->bone_count; bone_idx++) {
        if (SDL_strcmp(scene->bones[bone_idx].name, name) == 0) {
            return bone_idx;
        }
    }

    return -1;
}

/* Create an Object out of an aiNode */
static inline bool LoadObject(const struct aiScene *pScene, struct Scene *scene, const struct aiNode *pNode, struct Object *pObjectOut, struct Object *pParent) {
    static size_t mesh_idx;
    static struct aiMesh *mesh;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading object %s! (child of %s).\n", pNode->mName.data, (pParent ? pParent->name : "--none--"));

    pObjectOut->name = SDL_malloc(pNode->mName.length + 1);
    SDL_memcpy(pObjectOut->name, pNode->mName.data, pNode->mName.length + 1);

    aiDecomposeMatrix(&pNode->mTransformation, &pObjectOut->scale, &pObjectOut->rotation, &pObjectOut->position);

    pObjectOut->meshes = SDL_malloc(sizeof(struct Mesh) * pNode->mNumMeshes);
    pObjectOut->mesh_count = pNode->mNumMeshes;

    for (mesh_idx = 0; mesh_idx < pNode->mNumMeshes; mesh_idx++) {
        mesh = pScene->mMeshes[pNode->mMeshes[mesh_idx]];

        struct Vertex *vertices = SDL_malloc(sizeof(struct Vertex) * mesh->mNumVertices);

        /* It's not easy to predict the size of this array beforehand, so we need to dynamically resize this array as needed. */
        Sint32 *indices = NULL;

        /* size of the array, grows in increments of 1, and should be multiplied by 32. */
        size_t indices_size = 0;

        /* amount of indices */
        size_t index_count = 0;

        for (size_t vert_idx = 0; vert_idx < mesh->mNumVertices; vert_idx++) {
            vertices[vert_idx].vert[0] = mesh->mVertices[vert_idx].x;
            vertices[vert_idx].vert[1] = mesh->mVertices[vert_idx].y;
            vertices[vert_idx].vert[2] = mesh->mVertices[vert_idx].z;

            vertices[vert_idx].uv[0] = mesh->mTextureCoords[0][vert_idx].x;
            vertices[vert_idx].uv[1] = mesh->mTextureCoords[0][vert_idx].y;

            vertices[vert_idx].norm[0] = mesh->mNormals[vert_idx].x;
            vertices[vert_idx].norm[1] = mesh->mNormals[vert_idx].y;
            vertices[vert_idx].norm[2] = mesh->mNormals[vert_idx].z;

            vertices[vert_idx].bone_ids[0] = -1;
            vertices[vert_idx].bone_ids[1] = -1;
            vertices[vert_idx].bone_ids[2] = -1;
            vertices[vert_idx].bone_ids[3] = -1;
        }

        for (size_t bone_idx = 0; bone_idx < mesh->mNumBones; bone_idx++) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing bone '%s'\n", mesh->mBones[bone_idx]->mName.data);

            struct aiBone *bone = mesh->mBones[bone_idx];
            size_t bone_id = FindBoneByName(scene, bone->mName.data);
            if (bone_id == (size_t)-1) {
                scene->bones[bone_id = scene->bone_count++].name = SDL_malloc(bone->mName.length + 1);
                SDL_memcpy(scene->bones[bone_id].name, bone->mName.data, bone->mName.length + 1);

                scene->bones[bone_id].position_key_count = 0;
                scene->bones[bone_id].rotation_key_count = 0;
                scene->bones[bone_id].scale_key_count = 0;
            }

            scene->bones[bone_id].offset_matrix = bone->mOffsetMatrix;
            scene->bones[bone_id].local_transform = pNode->mTransformation;

            for (size_t weight_idx = 0; weight_idx < mesh->mBones[bone_idx]->mNumWeights; weight_idx++) {
                SDL_assert(bone->mWeights[weight_idx].mVertexId < mesh->mNumVertices);

                /* find an empty slot in the bone_ids/weights arrays (marked with -1) */
                for (size_t bone_ids_idx = 0; bone_ids_idx < 4; bone_ids_idx++) {
                    if (vertices[bone->mWeights[weight_idx].mVertexId].bone_ids[bone_ids_idx] < 0) {
                        vertices[bone->mWeights[weight_idx].mVertexId].bone_ids[bone_ids_idx] = bone_id;
                        vertices[bone->mWeights[weight_idx].mVertexId].weights[bone_ids_idx] = bone->mWeights[weight_idx].mWeight;

                        break;
                    }
                }
            }
        }

        for (size_t face_idx = 0; face_idx < mesh->mNumFaces; face_idx++) {
            index_count += mesh->mFaces[face_idx].mNumIndices;

            /* If the index count (including the new indices) outgrows the size of the array. */
            if (index_count > indices_size * 32) {
                /* Calculate "how many 32's can completely cover index_count" */
                size_t new_array_size = (int)SDL_ceilf((index_count + 1) / 32.f);

                /* Create an array with as many 32's as we need to completely cover index_count. */
                Sint32 *new_array = SDL_malloc(sizeof(Sint32) * new_array_size * 32);

                if (indices) {
                    SDL_memcpy(new_array, indices, sizeof(Sint32) * indices_size * 32);
                    SDL_free(indices);
                }

                indices = new_array;
                indices_size = new_array_size;
            }

            SDL_memcpy(&indices[index_count - (mesh->mFaces[face_idx].mNumIndices)], mesh->mFaces[face_idx].mIndices, mesh->mFaces[face_idx].mNumIndices * sizeof(Sint32));
        }

        if (!CreateVertexBuffer(vertices, mesh->mNumVertices, &pObjectOut->meshes[mesh_idx].vertex_buffer)) {
            return false;
        }

        if (!CreateIndexBuffer(indices, index_count, &pObjectOut->meshes[mesh_idx].index_buffer)) {
            return false;
        }

        static struct aiColor4D diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        static struct aiColor4D specular = { 1.0f, 1.0f, 1.0f, 1.0f };
        static struct aiColor4D ambient = { 0.2f, 0.2f, 0.2f, 1.0f };

        aiGetMaterialColor(pScene->mMaterials[mesh->mMaterialIndex], AI_MATKEY_COLOR_DIFFUSE, &diffuse);
        aiGetMaterialColor(pScene->mMaterials[mesh->mMaterialIndex], AI_MATKEY_COLOR_SPECULAR, &specular);
        aiGetMaterialColor(pScene->mMaterials[mesh->mMaterialIndex], AI_MATKEY_COLOR_AMBIENT, &ambient);

        /* discard .a */
        pObjectOut->meshes[mesh_idx].material.diffuse[0] = diffuse.r;
        pObjectOut->meshes[mesh_idx].material.diffuse[1] = diffuse.g;
        pObjectOut->meshes[mesh_idx].material.diffuse[2] = diffuse.b;

        pObjectOut->meshes[mesh_idx].material.specular[0] = specular.r;
        pObjectOut->meshes[mesh_idx].material.specular[1] = specular.g;
        pObjectOut->meshes[mesh_idx].material.specular[2] = specular.b;

        pObjectOut->meshes[mesh_idx].material.ambient[0] = ambient.r;
        pObjectOut->meshes[mesh_idx].material.ambient[1] = ambient.g;
        pObjectOut->meshes[mesh_idx].material.ambient[2] = ambient.b;

        pObjectOut->meshes[mesh_idx].material.shininess = 0;
        aiGetMaterialFloat(pScene->mMaterials[mesh->mMaterialIndex], AI_MATKEY_SHININESS, &pObjectOut->meshes[mesh_idx].material.shininess);
        if (pObjectOut->meshes[mesh_idx].material.shininess == 0) {
            pObjectOut->meshes[mesh_idx].material.shininess = 32;
        }

        if (aiGetMaterialTextureCount(pScene->mMaterials[mesh->mMaterialIndex], aiTextureType_DIFFUSE) > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "diffuse texture detected, using textured shader!\n");
    
            if (!textured_test_pipeline && !InitTexturedTestPipeline()) {
                return false;
            }

            struct aiString path;
            if (aiGetMaterialTexture(pScene->mMaterials[mesh->mMaterialIndex], aiTextureType_DIFFUSE, 0, &path, NULL, NULL, NULL, NULL, NULL, NULL) != aiReturn_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get material texture!\n");
                return false;
            }

            struct SDL_Surface *texture_surface;
            /* Embedded textures in Assimp start with an asterisk and end in an index to pScene->mTextures[] */
            if (path.length >= 2 && path.data[0] == '*') {
                Uint32 idx = SDL_atoi(&path.data[1]);

                assert(idx < pScene->mNumTextures);
                
                /* some embedded textures are loaded as raw compressed data, in which case we just simply load it with SDL_image. */
                if (pScene->mTextures[idx]->mHeight == 0) {
                    SDL_IOStream *stream = SDL_IOFromMem(pScene->mTextures[idx]->pcData, pScene->mTextures[idx]->mWidth);
                    if (!(texture_surface = IMG_Load_IO(stream, true))) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load mesh texture! (SDL Error: %s)\n", SDL_GetError());
                        return false;
                    }
                } else {
                    /* the format is static, meaning we can hardcode the pitch multiplier (4 bytes per pixel), and the format.
                     * the lifetime of the texture pixel data also outlives the surface. which is important because this function doesn't copy the pixel data. */
                    if (!(texture_surface = SDL_CreateSurfaceFrom(pScene->mTextures[idx]->mWidth, pScene->mTextures[idx]->mHeight, SDL_PIXELFORMAT_ARGB8888, pScene->mTextures[idx]->pcData, pScene->mTextures[idx]->mWidth * 4))) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load mesh texture! (SDL Error: %s)\n", SDL_GetError());
                        return false;
                    }
                }
            } else {
                /* the `path` variable is local to the models folder, we have to prefix it with 'models/' (7 chars) */
                char *rel_path = SDL_malloc(7 + path.length + 1);
                strcpy(rel_path, "models/");
                if (SDL_strcmp(SDL_GetPlatform(), "Windows") == 0) {
                    /* oh look at me im quirky i use \ instead of / */
                    rel_path[6] = '\\';
                }
                strncat(rel_path, path.data, path.length);
                rel_path[7 + path.length] = '\0';

                if (!(texture_surface = IMG_Load(rel_path))) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load mesh texture! (SDL Error: %s)\n", SDL_GetError());
                    return false;
                }

                SDL_free(rel_path);
            }

            /* stupid sampler has to be a float and we can't use 'char' as a substitute */
            struct SDL_Surface *new_surface = SDL_ConvertSurface(texture_surface, SDL_PIXELFORMAT_RGBA64_FLOAT);
            SDL_DestroySurface(texture_surface);
            texture_surface = new_surface;

            SDL_GPUTextureCreateInfo gpu_texture_create_info;
            gpu_texture_create_info.type = SDL_GPU_TEXTURETYPE_2D;
            gpu_texture_create_info.props = 0;
            gpu_texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            gpu_texture_create_info.width = texture_surface->w;
            gpu_texture_create_info.height = texture_surface->h;
            gpu_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            gpu_texture_create_info.num_levels = 1;
            gpu_texture_create_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            gpu_texture_create_info.layer_count_or_depth = 1;

            if (!(pObjectOut->meshes[mesh_idx].texture.gpu_texture = SDL_CreateGPUTexture(gpu_device, &gpu_texture_create_info))) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GPU texture! (SDL Error: %s)\n", SDL_GetError());
                return false;
            }

            if (!CopySurfaceToTexture(texture_surface, pObjectOut->meshes[mesh_idx].texture.gpu_texture)) {
                return false;
            }

            SDL_DestroySurface(texture_surface);

            static SDL_GPUSamplerCreateInfo sampler_create_info;
            sampler_create_info.props = 0;
            sampler_create_info.enable_anisotropy = false;
            sampler_create_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler_create_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler_create_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler_create_info.enable_compare = true;
            sampler_create_info.mip_lod_bias = 0.0f;
            sampler_create_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            sampler_create_info.min_filter = SDL_GPU_FILTER_LINEAR;
            sampler_create_info.mag_filter = SDL_GPU_FILTER_LINEAR;
            sampler_create_info.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            sampler_create_info.min_lod = 0.0f;
            sampler_create_info.max_lod = 0.0f;

            if (!(pObjectOut->meshes[mesh_idx].texture.gpu_sampler = SDL_CreateGPUSampler(gpu_device, &sampler_create_info))) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GPU sampler! (SDL Error: %s)\n", SDL_GetError());
                return false;
            }

            pObjectOut->meshes[mesh_idx].shader = TEXTURED_TEST_SHADER;
        } else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "diffuse texture not found, using untextured shader!\n");
            
            if (!untextured_test_pipeline && !InitUntexturedTestPipeline()) {
                return false;
            }

            pObjectOut->meshes[mesh_idx].shader = UNTEXTURED_TEST_SHADER;
        }

        SDL_free(vertices);
        SDL_free(indices);
    }

    pObjectOut->scene = scene;
    pObjectOut->parent = pParent;

    return true;
}

/* Get objects_array's size, the array is sized at the nearest 32. */
static inline size_t GetObjectsSize() {
    return (int)SDL_ceilf(objects_count / 32.f) * 32;
}

/* Make space for an extra object */
static inline struct Object *EmplaceObject() {
    /* Should we resize the array? True if we surpass a multiple of 32. */
    bool resize_array = objects_count % 33 == 0;

    objects_count++;
    if (resize_array) {
        struct Object **new_array = SDL_malloc(sizeof(struct Object *) * GetObjectsSize());

        if (objects_array) {
            /* Do this so that GetObjectsSize() is the old size */
            objects_count--;

            SDL_memcpy(new_array, objects_array, GetObjectsSize());
            SDL_free(objects_array);

            objects_count++;
        }

        objects_array = new_array;
    }

    return (objects_array[objects_count - 1] = SDL_malloc(sizeof(struct Object)));
}

/* Recursively load all the objects in the scene starting from node (and its children) */
static inline bool LoadSceneObjects(const struct aiScene *aiScene, struct Scene *scene, const struct aiNode *node, struct Object *parent) {
    struct Object *object = EmplaceObject();
    if (!LoadObject(aiScene, scene, node, object, parent)) {
        return false;
    }
    for (size_t i = 0; i < node->mNumChildren; i++) {
        if (!LoadSceneObjects(aiScene, scene, node->mChildren[i], object)) {
            return false;
        }
    }

    return true;
}

/* Search a node (and its children, recursively) for a node with the name `name`. returns NULL on fail. */
static inline const struct aiNode *GetNodeByName(const struct aiNode *pNode, const char *name, size_t len) {
    if (SDL_strncmp(pNode->mName.data, name, SDL_min(pNode->mName.length, len)) == 0) {
        return pNode;
    }

    static struct aiNode *child;
    for (size_t i = 0; i < pNode->mNumChildren; i++) {
        child = pNode->mChildren[i];

        if (GetNodeByName(child, name, len)) {
            return child;
        }
    }

    return NULL;
}

static inline void AppendPlayerObject(struct PlayerObjectList *pPlayerObject) {
    if (!player_objects) {
        player_objects = pPlayerObject;
        return;
    }

    struct PlayerObjectList *head = player_objects;
    while (head->next) {
        head = head->next;
    }

    head->next = pPlayerObject;
    pPlayerObject->prev = head;
}

/* Loads all the models and stuff necessary for the game! */
static inline bool LoadScene() {
    const struct aiScene *aiScene = aiImportFile("models/test.glb", 0);
    
    intro_scene = SDL_malloc(sizeof(struct Scene));
    intro_scene->bone_count = 0;
    intro_scene->animation_playing = false;
    intro_scene->current_animation.duration = 0;

    if (!aiScene) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to import 'models/test.glb'!\n");
        return false;
    }

    if (aiScene->mNumAnimations > 0) {
        struct aiAnimation *animation = aiScene->mAnimations[0];

        intro_scene->animation_playing = true;
        intro_scene->current_animation.duration = animation->mDuration;
        intro_scene->current_animation.ticks_per_sec = animation->mTicksPerSecond;
        
        for (size_t channel_idx = 0; channel_idx < animation->mNumChannels; channel_idx++) {
            struct aiNodeAnim *channel = animation->mChannels[channel_idx];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing channel '%s'!\n", channel->mNodeName.data);

            intro_scene->bones[intro_scene->bone_count].name = SDL_malloc(channel->mNodeName.length + 1);
            SDL_memcpy(intro_scene->bones[intro_scene->bone_count].name, channel->mNodeName.data, channel->mNodeName.length + 1);

            intro_scene->bones[intro_scene->bone_count].position_key_count = channel->mNumPositionKeys;
            intro_scene->bones[intro_scene->bone_count].position_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * intro_scene->bones[intro_scene->bone_count].position_key_count);
            for (size_t position_key_idx = 0; position_key_idx < channel->mNumPositionKeys; position_key_idx++) {
                intro_scene->bones[intro_scene->bone_count].position_keys[position_key_idx].value = channel->mPositionKeys[position_key_idx].mValue;
                intro_scene->bones[intro_scene->bone_count].position_keys[position_key_idx].timestamp = channel->mPositionKeys[position_key_idx].mTime;
            }

            intro_scene->bones[intro_scene->bone_count].rotation_key_count = channel->mNumRotationKeys;
            intro_scene->bones[intro_scene->bone_count].rotation_keys = SDL_malloc(sizeof(struct QuatKeyframe) * intro_scene->bones[intro_scene->bone_count].rotation_key_count);
            for (size_t rotation_key_idx = 0; rotation_key_idx < channel->mNumRotationKeys; rotation_key_idx++) {
                intro_scene->bones[intro_scene->bone_count].rotation_keys[rotation_key_idx].value = channel->mRotationKeys[rotation_key_idx].mValue;
                intro_scene->bones[intro_scene->bone_count].rotation_keys[rotation_key_idx].timestamp = channel->mRotationKeys[rotation_key_idx].mTime;
            }

            intro_scene->bones[intro_scene->bone_count].scale_key_count = channel->mNumScalingKeys;
            intro_scene->bones[intro_scene->bone_count].scale_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * intro_scene->bones[intro_scene->bone_count].scale_key_count);
            for (size_t scale_key_idx = 0; scale_key_idx < channel->mNumScalingKeys; scale_key_idx++) {
                intro_scene->bones[intro_scene->bone_count].scale_keys[scale_key_idx].value = channel->mScalingKeys[scale_key_idx].mValue;
                intro_scene->bones[intro_scene->bone_count].scale_keys[scale_key_idx].timestamp = channel->mScalingKeys[scale_key_idx].mTime;
            }

            aiIdentityMatrix4(&intro_scene->bones[intro_scene->bone_count++].local_transform);
        }
    }

    if (!LoadSceneObjects(aiScene, intro_scene, aiScene->mRootNode, NULL)) {
        return false;
    }

    for (size_t i = 0; i < aiScene->mNumLights; i++) {
        static struct aiLight *light;
        light = aiScene->mLights[i];
        static const struct aiNode *corresponding_node;
        corresponding_node = GetNodeByName(aiScene->mRootNode, light->mName.data, light->mName.length);
 
        static struct aiVector3D position;
        static struct aiQuaternion _;
        static struct aiVector3D _1;
        aiDecomposeMatrix(&corresponding_node->mTransformation, &_1, &_, &position);
        aiVector3Add(&position, &light->mPosition);

        /* TODO: temporary, maybe there's a better solution.
         * Doing this to avoid stupid high diffuse values. */
        struct aiVector3D diffuse = {light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b};
        struct aiVector3D specular = {light->mColorSpecular.r, light->mColorSpecular.g, light->mColorSpecular.b};
        struct aiVector3D ambient = {light->mColorAmbient.r, light->mColorAmbient.g, light->mColorAmbient.b};
        aiVector3DivideByScalar(&diffuse, SDL_max(SDL_max(SDL_max(light->mColorDiffuse.r, light->mColorDiffuse.g), light->mColorDiffuse.b), 1.0));
        aiVector3DivideByScalar(&specular, SDL_max(SDL_max(SDL_max(light->mColorSpecular.r, light->mColorSpecular.g), light->mColorSpecular.b), 1.0));
        aiVector3DivideByScalar(&ambient, SDL_max(SDL_max(SDL_max(light->mColorAmbient.r, light->mColorAmbient.g), light->mColorAmbient.b), 1.0));

        lights.lights[lights.lights_count].pos[0] = position.x;
        lights.lights[lights.lights_count].pos[1] = position.y;
        lights.lights[lights.lights_count].pos[2] = position.z;

        lights.lights[lights.lights_count].diffuse[0] = diffuse.x;
        lights.lights[lights.lights_count].diffuse[1] = diffuse.y;
        lights.lights[lights.lights_count].diffuse[2] = diffuse.z;

        lights.lights[lights.lights_count].specular[0] = specular.x;
        lights.lights[lights.lights_count].specular[1] = specular.y;
        lights.lights[lights.lights_count].specular[2] = specular.z;
        
        lights.lights[lights.lights_count].ambient[0] = ambient.x;
        lights.lights[lights.lights_count].ambient[1] = ambient.y;
        lights.lights[lights.lights_count++].ambient[2] = ambient.z;
    }

    aiReleaseImport(aiScene);

    const struct aiScene *character_scene = aiImportFile("models/character.glb", 0);

    if (!character_scene) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to import 'models/character.glb'!\n");
        return false;
    }

    /* The actual 'Character' node that we're looking for in the scene. */
    const struct aiNode *character_node = GetNodeByName(character_scene->mRootNode, "Character", 10);

    if (!character_node) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to find 'Character' node in character scene!\n");
        return false;
    }

    const struct PlayersLinkedList *players = NETGetPlayers();

    if (!players) {
        /* Should be impossible.. */
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get players! no players found! (\?\?\?)\n");
        return false;
    }

    do {
        if (players->ts.id == NETGetSelfID()) {
            SDL_memcpy(camera_pos, players->ts.position, sizeof(camera_pos));
            players = players->next;
            continue;
        }

        struct PlayerObjectList *player_obj = SDL_malloc(sizeof(struct PlayerObjectList));

        player_obj->prev = NULL;

        player_obj->obj = EmplaceObject();
        player_obj->id = players->ts.id;

        player_obj->next = NULL;

        /* TODO: shouldn't be null */
        if (!LoadObject(character_scene, NULL, character_node, player_obj->obj, NULL)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load character node!\n");
            return false;
        }

        player_obj->obj->position.x = players->ts.position[0];
        player_obj->obj->position.y = players->ts.position[1];
        player_obj->obj->position.z = players->ts.position[2];

        player_obj->obj->rotation.x = players->ts.rotation[0];
        player_obj->obj->rotation.y = players->ts.rotation[1];
        player_obj->obj->rotation.z = players->ts.rotation[2];
        player_obj->obj->rotation.w = players->ts.rotation[3];
        
        player_obj->obj->scale.x = players->ts.scale[0];
        player_obj->obj->scale.y = players->ts.scale[1];
        player_obj->obj->scale.z = players->ts.scale[2];

        AppendPlayerObject(player_obj);

        players = players->next;
    } while (players);

    return true;
}

static void GoToMainMenu(const ConnectionHandle _, [[gnu::unused]] const char *const _pReason) {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

static inline struct PlayerObjectList *GetPlayerObjectByID(int id) {
    if (!player_objects) {
        return NULL;
    }

    struct PlayerObjectList *i = player_objects;
    while (i && i->id != id) {
        i = i->next;
    }

    return i;
}

static void OnPlayerUpdate(const ConnectionHandle _, const struct Player * const player) {
    if (player->id == NETGetSelfID()) {
        SDL_memcpy(camera_pos, player->position, sizeof(camera_pos));
        return;
    }
    
    const struct PlayerObjectList * player_obj = GetPlayerObjectByID(player->id);

    if (!player_obj) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Got a PlayerUpdate, but we couldn't find the player object!\n");
        return;
    }

    player_obj->obj->position.x = player->position[0];
    player_obj->obj->position.y = player->position[1];
    player_obj->obj->position.z = player->position[2];

    player_obj->obj->rotation.x = player->rotation[0];
    player_obj->obj->rotation.y = player->rotation[1];
    player_obj->obj->rotation.z = player->rotation[2];
    player_obj->obj->rotation.w = player->rotation[3];
    
    player_obj->obj->scale.x = player->scale[0];
    player_obj->obj->scale.y = player->scale[1];
    player_obj->obj->scale.z = player->scale[2];
}

bool IntroInit(SDL_GPUDevice *pGPUDevice) {
    gpu_device = pGPUDevice;

    LEGrabMouse();

    lights.lights_count = 0;

    glm_mat4_identity(matrices.view);
    glm_mat4_identity(matrices.projection);

    //glm_lookat(camera_pos, (vec3){0, 0, 0}, (vec3){0, 1, 0}, matrices.view);

    NETSetClientDisconnectCallback(GoToMainMenu);
    NETSetClientUpdateCallback(OnPlayerUpdate);

    return true;
}

static inline void aiVector3Lerp(struct aiVector3D *pFirst, struct aiVector3D *pSecond, double t, struct aiVector3D *pOut) {
    pOut->x = pFirst->x * (1.0 - t) + pSecond->x * t;
    pOut->y = pFirst->y * (1.0 - t) + pSecond->y * t;
    pOut->z = pFirst->z * (1.0 - t) + pSecond->z * t;
}

static inline void aiQuaternionLerp(struct aiQuaternion *pFirst, struct aiQuaternion *pSecond, double t, struct aiQuaternion *pOut) {
    pOut->x = pFirst->x * (1.0 - t) + pSecond->x * t;
    pOut->y = pFirst->y * (1.0 - t) + pSecond->y * t;
    pOut->z = pFirst->z * (1.0 - t) + pSecond->z * t;
    pOut->w = pFirst->w * (1.0 - t) + pSecond->w * t;
    aiQuaternionNormalize(pOut);
}

static inline void aiMatrix4ToMat4(mat4 *dst, struct aiMatrix4x4 *src) {
    (*dst)[0][0] = src->a1;
    (*dst)[0][1] = src->b1;
    (*dst)[0][2] = src->c1;
    (*dst)[0][3] = src->d1;

    (*dst)[1][0] = src->a2;
    (*dst)[1][1] = src->b2;
    (*dst)[1][2] = src->c2;
    (*dst)[1][3] = src->d2;

    (*dst)[2][0] = src->a3;
    (*dst)[2][1] = src->b3;
    (*dst)[2][2] = src->c3;
    (*dst)[2][3] = src->d3;

    (*dst)[3][0] = src->a4;
    (*dst)[3][1] = src->b4;
    (*dst)[3][2] = src->c4;
    (*dst)[3][3] = src->d4;
}

static inline void StepAnimation(struct Scene *scene) {
    if (scene->animation_playing) {
        scene->animation_time += LEFrametime * scene->current_animation.ticks_per_sec;
        
        if (scene->animation_time >= scene->current_animation.duration) {
            scene->animation_time = SDL_fmod(scene->animation_time, scene->current_animation.duration);
        }

        /* update all bone local transforms */
        for (size_t bone_idx = 0; bone_idx < scene->bone_count; bone_idx++) {
            struct aiVector3D position = {0, 0, 0};
            struct aiVector3D scale = {1, 1, 1};
            struct aiQuaternion rotation = {1, 0, 0, 0};

            struct Bone *bone = &scene->bones[bone_idx];

            size_t key_idx = 0;
            for (key_idx = 0; key_idx < bone->position_key_count; key_idx++) {
                if (bone->position_keys[key_idx].timestamp < scene->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    position = bone->position_keys->value;
                    break;
                }

                double last_timestamp = bone->position_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->position_keys[key_idx].timestamp;

                aiVector3Lerp(&bone->position_keys[key_idx - 1].value, &bone->position_keys[key_idx].value, (scene->animation_time - last_timestamp) / (new_timestamp - last_timestamp), &position);
                break;
            }

            for (key_idx = 0; key_idx < bone->rotation_key_count; key_idx++) {
                if (bone->rotation_keys[key_idx].timestamp < scene->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    rotation = bone->rotation_keys->value;
                    break;
                }

                double last_timestamp = bone->rotation_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->rotation_keys[key_idx].timestamp;

                aiQuaternionLerp(&bone->rotation_keys[key_idx - 1].value, &bone->rotation_keys[key_idx].value, (scene->animation_time - last_timestamp) / (new_timestamp - last_timestamp), &rotation);
                break;
            }

            for (key_idx = 0; key_idx < bone->scale_key_count; key_idx++) {
                if (bone->scale_keys[key_idx].timestamp < scene->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    scale = bone->scale_keys->value;
                    break;
                }

                double last_timestamp = bone->scale_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->scale_keys[key_idx].timestamp;

                aiVector3Lerp(&bone->scale_keys[key_idx - 1].value, &bone->scale_keys[key_idx].value, (scene->animation_time - last_timestamp) / (new_timestamp - last_timestamp), &scale);
                break;
            }

            aiMatrix4FromScalingQuaternionPosition(&bone->local_transform, &scale, &rotation, &position);
        }
    }
    
    for (size_t obj_idx = 0; obj_idx < objects_count; obj_idx++) {
        if (objects_array[obj_idx]->scene != scene) {
            continue;
        }
        size_t bone_id = FindBoneByName(scene, objects_array[obj_idx]->name);
        if (bone_id == (size_t)-1) {
            aiMatrix4FromScalingQuaternionPosition(&objects_array[obj_idx]->transformation, &objects_array[obj_idx]->scale, &objects_array[obj_idx]->rotation, &objects_array[obj_idx]->position);
        } else {
            objects_array[obj_idx]->transformation = scene->bones[bone_id].local_transform;
        }

        if (objects_array[obj_idx]->parent) {
            aiMultiplyMatrix4(&objects_array[obj_idx]->transformation, &objects_array[obj_idx]->parent->transformation);
        }

        if (bone_id != (size_t)-1) {
            mat4 offset_matrix;
            aiMatrix4ToMat4(&offset_matrix, &scene->bones[bone_id].offset_matrix);

            aiMatrix4ToMat4(&matrices.bone_matrices[bone_id], &objects_array[obj_idx]->transformation);
            glm_mat4_mul(matrices.bone_matrices[bone_id], offset_matrix, matrices.bone_matrices[bone_id]);
        }
    }
}

bool IntroRender(void) {
    /* initialize scene if not initialized, exit on failure */
    if (!objects_array && !LoadScene()) {
        return false;
    }

    StepAnimation(intro_scene);

    camera_pitch = SDL_min(SDL_max(camera_pitch + -LEMouseRelY * LEFrametime, -1.15f), 0.8f);
    camera_yaw = SDL_fmodf(camera_yaw + -LEMouseRelX * LEFrametime, 6.28f);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "pitch: %f, yaw: %f\n", camera_pitch, camera_yaw);
    NETChangeCameraDirection((vec2){camera_pitch, camera_yaw});

    glm_perspective(1.0472f, (float)LESwapchainWidth/(float)LESwapchainHeight, 0.1f, 1000.f, matrices.projection);

    if (NETGetSelfID() >= 0) {
        static vec3 dir_vec;
        dir_vec[0] = 1.0f;
        dir_vec[1] = 0.0f;
        dir_vec[2] = 0.0f;
        glm_quat_rotatev((float *)NETGetPlayerByID(NETGetSelfID())->rotation, dir_vec, dir_vec);
        glm_look(camera_pos, dir_vec, (vec3){0, 1, 0}, matrices.view);
    } else {
        /* uhm */
        glm_look(camera_pos, (vec3){-1.f, 0.f, 0.f}, (vec3){0, 1, 0}, matrices.view);
    }

    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){0.f, 0.f, 0.f, 1.f};
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = LESwapchainTexture;

    static SDL_GPUDepthStencilTargetInfo depth_stencil_target_info;
    depth_stencil_target_info.clear_stencil = 255;
    depth_stencil_target_info.clear_depth = 1.0f;
    depth_stencil_target_info.cycle = false;
    depth_stencil_target_info.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_stencil_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target_info.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target_info.texture = LEDepthStencilTexture;

    static SDL_GPURenderPass *render_pass;

    SDL_GPUViewport viewport = {0, 0, LESwapchainWidth, LESwapchainHeight, 0.0f, 1.0f};

    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, &depth_stencil_target_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_SetGPUViewport(render_pass, &viewport);

    struct Object *obj;
    for (size_t i = 0; i < objects_count; i++) {
        obj = objects_array[i];

        for (size_t mesh_idx = 0; mesh_idx < obj->mesh_count; mesh_idx++) {
            struct Mesh *mesh = &obj->meshes[mesh_idx];

            switch (mesh->shader) {
            case UNTEXTURED_TEST_SHADER:
                SDL_BindGPUGraphicsPipeline(render_pass, untextured_test_pipeline);
                break;
            case TEXTURED_TEST_SHADER:
                SDL_BindGPUGraphicsPipeline(render_pass, textured_test_pipeline);
                break;
            }

            SDL_GPUBufferBinding vertex_buffer_binding;
            vertex_buffer_binding.buffer = mesh->vertex_buffer.buffer;
            vertex_buffer_binding.offset = 0;

            SDL_GPUBufferBinding index_buffer_binding;
            index_buffer_binding.buffer = mesh->index_buffer.buffer;
            index_buffer_binding.offset = 0;

            SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_buffer_binding, 1);
            SDL_BindGPUIndexBuffer(render_pass, &index_buffer_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            glm_mat4_identity(matrices.model);


            /* move up in the hierarchy (starting with the object) and apply transformations */
            struct Object *head = obj;
            while (head) {
                glm_translate(matrices.model, (vec3){ head->position.x, head->position.y, head->position.z });
                glm_quat_rotate(matrices.model, (vec4){ head->rotation.x, head->rotation.y, head->rotation.z, head->rotation.w }, matrices.model);
                glm_scale(matrices.model, (vec3){ head->scale.x, head->scale.y, head->scale.z });

                head = head->parent;
            }

            SDL_PushGPUVertexUniformData(LECommandBuffer, 0, &matrices, sizeof(matrices));

            SDL_PushGPUFragmentUniformData(LECommandBuffer, 0, &mesh->material, sizeof(mesh->material));
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 1, &lights, sizeof(lights));
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 2, &camera_pos, sizeof(camera_pos));

            if (mesh->shader == TEXTURED_TEST_SHADER) {
                SDL_GPUTextureSamplerBinding sampler_binding;
                sampler_binding.texture = mesh->texture.gpu_texture;
                sampler_binding.sampler = mesh->texture.gpu_sampler;

                SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
            }

            SDL_DrawGPUIndexedPrimitives(render_pass, mesh->index_buffer.count, 1, 0, 0, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);

    return true;
}

void IntroCleanup(void) {
    LEReleaseMouse();

    if (intro_scene) {
        for (; intro_scene->bone_count > 0; intro_scene->bone_count--) {
            struct Bone *bone = &intro_scene->bones[intro_scene->bone_count - 1];

            if (bone->name) {
                SDL_free(bone->name);
            }

            /* We don't need to check the other values, because if there's atleast one position key, there's atleast one of every other key. */
            if (bone->position_key_count > 0) {
                SDL_free(bone->position_keys);
                SDL_free(bone->rotation_keys);
                SDL_free(bone->scale_keys);
            }
        }

        SDL_free(intro_scene);
    }

    for (; objects_count > 0; objects_count--) {
        struct Object *object = objects_array[objects_count - 1];

        if (object->name) {
            SDL_free(object->name);
        }

        for (; object->mesh_count > 0; object->mesh_count--) {
            SDL_ReleaseGPUBuffer(gpu_device, object->meshes[object->mesh_count - 1].vertex_buffer.buffer);
            SDL_ReleaseGPUBuffer(gpu_device, object->meshes[object->mesh_count - 1].index_buffer.buffer);

            if (object->meshes[object->mesh_count - 1].shader == TEXTURED_TEST_SHADER) {
                SDL_ReleaseGPUSampler(gpu_device, object->meshes[object->mesh_count - 1].texture.gpu_sampler);
                SDL_ReleaseGPUTexture(gpu_device, object->meshes[object->mesh_count - 1].texture.gpu_texture);
            }
        }

        SDL_free(object->meshes);

        SDL_free(object);
    }
    if (objects_array) {
        SDL_free(objects_array);
        objects_array = NULL;
    }

    for (; player_objects; player_objects = player_objects->next) {
        if (player_objects->prev) {
            SDL_free(player_objects->prev);
        }
    }
    /* the loop above won't free the last element, so we have to do it manually */
    if (player_objects) {
        SDL_free(player_objects);
        player_objects = NULL;
    }

    /* these objects *may* be uninitialized, which is gonna be a real pain to free. */
    if (untextured_test_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, untextured_test_pipeline);
        SDL_ReleaseGPUShader(gpu_device, untextured_test_shader.vertex);
        SDL_ReleaseGPUShader(gpu_device, untextured_test_shader.fragment);
        untextured_test_pipeline = NULL;
    }
    if (textured_test_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, textured_test_pipeline);
        SDL_ReleaseGPUShader(gpu_device, textured_test_shader.vertex);
        SDL_ReleaseGPUShader(gpu_device, textured_test_shader.fragment);
        textured_test_pipeline = NULL;
    }
}
