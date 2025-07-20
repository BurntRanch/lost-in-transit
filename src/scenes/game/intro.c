#include "scenes/game/intro.h"
#include "cglm/types.h"
#include "engine.h"
#include "networking.h"
#include "scenes.h"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <assimp/defs.h>
#include <assimp/mesh.h>
#include <assimp/vector3.h>
#include <cglm/cglm.h>
#include <stddef.h>
#include <stdio.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>

struct Shader {
    SDL_GPUShader* vertex;
    SDL_GPUShader* fragment;
};

struct Vertex {
    vec3 vert;
};

struct Buffer {
    struct SDL_GPUBuffer* buffer;

    /* amount of elements */
    size_t count;
};

/* An object in the game world */
struct Object {
    struct aiVector3D position;
    struct aiQuaternion rotation;
    struct aiVector3D scale;

    struct Buffer* vertex_buffers;
    struct Buffer* index_buffers;

    /* vertex_buffers and index_buffers are the same length */
    size_t buffers_count;
};

static SDL_GPUDevice* gpu_device = NULL;

static struct Shader test_shader;
static SDL_GPUGraphicsPipeline* test_pipeline = NULL;

static struct Object** objects_array = NULL;
static Uint32 objects_count = 0;

/* Don't laugh */
static inline bool LoadShader(const char* fileName, Uint8** ppBufferOut, size_t* pSizeOut) {
    void* data = NULL;

    if (!(data = SDL_LoadFile(fileName, pSizeOut))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open shader file %s! (SDL Error: %s)\n", fileName, SDL_GetError());
        return false;
    }

    *ppBufferOut = data;

    return true;
}

static inline bool InitTestPipeline() {
    SDL_GPUShaderCreateInfo vertex_shader_create_info, fragment_shader_create_info;
    vertex_shader_create_info.code = NULL;
    vertex_shader_create_info.code_size = sizeof(NULL);
    vertex_shader_create_info.entrypoint = "main";
    vertex_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_shader_create_info.num_samplers = 0;
    vertex_shader_create_info.num_storage_buffers = 0;
    vertex_shader_create_info.num_storage_textures = 0;
    vertex_shader_create_info.num_uniform_buffers = 0;
    vertex_shader_create_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_shader_create_info.props = 0;

    if (!LoadShader("shaders/test_shader.vert.spv", (Uint8**)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size)) {
        return false;
    }

    fragment_shader_create_info.code = NULL;
    fragment_shader_create_info.code_size = sizeof(NULL);
    fragment_shader_create_info.entrypoint = "main";
    fragment_shader_create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_shader_create_info.num_samplers = 0;
    fragment_shader_create_info.num_storage_buffers = 0;
    fragment_shader_create_info.num_storage_textures = 0;
    fragment_shader_create_info.num_uniform_buffers = 0;
    fragment_shader_create_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_shader_create_info.props = 0;

    if (!LoadShader("shaders/test_shader.frag.spv", (Uint8**)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
        return false;
    }

    if (!(test_shader.vertex = SDL_CreateGPUShader(gpu_device, &vertex_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test vertex GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(test_shader.fragment = SDL_CreateGPUShader(gpu_device, &fragment_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test fragment GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_free((void*)vertex_shader_create_info.code);
    SDL_free((void*)fragment_shader_create_info.code);

    struct SDL_GPUColorTargetDescription color_target_description;
    color_target_description.blend_state.enable_color_write_mask = false;
    color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
    color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    color_target_description.blend_state.enable_blend = false;
    color_target_description.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

    struct SDL_GPUVertexBufferDescription vertex_buffer_description;
    vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_description.instance_step_rate = 0;
    vertex_buffer_description.pitch = sizeof(struct Vertex);
    vertex_buffer_description.slot = 0;

    struct SDL_GPUVertexAttribute vertex_attribute;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attribute.location = 0;
    vertex_attribute.offset = 0;

    struct SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.target_info.has_depth_stencil_target = false;
    graphics_pipeline_create_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_A8_UNORM;
    graphics_pipeline_create_info.target_info.color_target_descriptions = &color_target_description;
    graphics_pipeline_create_info.target_info.num_color_targets = 1;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.back_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_REPLACE;
    graphics_pipeline_create_info.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    graphics_pipeline_create_info.depth_stencil_state.enable_stencil_test = false;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_test = false;
    graphics_pipeline_create_info.depth_stencil_state.enable_depth_write = false;
    graphics_pipeline_create_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    graphics_pipeline_create_info.multisample_state.sample_mask = 0;
    graphics_pipeline_create_info.multisample_state.enable_mask = false;
    graphics_pipeline_create_info.rasterizer_state.depth_bias_clamp = 0.0;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_bias = true;
    graphics_pipeline_create_info.rasterizer_state.enable_depth_clip = false;
    graphics_pipeline_create_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    graphics_pipeline_create_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    graphics_pipeline_create_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_attributes = 1;
    graphics_pipeline_create_info.vertex_input_state.vertex_attributes = &vertex_attribute;
    graphics_pipeline_create_info.vertex_input_state.num_vertex_buffers = 1;
    graphics_pipeline_create_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
    graphics_pipeline_create_info.vertex_shader = test_shader.vertex;
    graphics_pipeline_create_info.fragment_shader = test_shader.fragment;
    graphics_pipeline_create_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    graphics_pipeline_create_info.props = 0;

    if (!(test_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &graphics_pipeline_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test graphics pipeline! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

static inline bool CreateVertexBuffer(const struct Vertex* pVertices, size_t vertexCount, struct Buffer* pVertexBufferOut) {
    SDL_GPUCopyPass* copy_pass;

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

    static SDL_GPUTransferBuffer* transfer_buffer = NULL;

    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = vertex_buffer_create_info.size;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    if (!(transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);

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

    pVertexBufferOut->count = vertexCount;

    return true;
}

static inline bool CreateIndexBuffer(const Sint32* pIndices, size_t indexCount, struct Buffer* pIndexBufferOut) {
    SDL_GPUCopyPass* copy_pass;

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

    static SDL_GPUTransferBuffer* transfer_buffer = NULL;

    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = index_buffer_create_info.size;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    if (!(transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    void* data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);

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

    pIndexBufferOut->count = indexCount;

    return true;
}

/* Create an Object out of an aiNode */
static inline bool LoadObject(const struct aiScene* pScene, const struct aiNode* pNode, struct Object* pObjectOut) {
    static size_t mesh_idx;
    static struct aiMesh* mesh;

    aiDecomposeMatrix(&pNode->mTransformation, &pObjectOut->scale, &pObjectOut->rotation, &pObjectOut->position);

    pObjectOut->vertex_buffers = SDL_malloc(sizeof(struct Buffer*) * pNode->mNumMeshes);
    pObjectOut->index_buffers = SDL_malloc(sizeof(struct Buffer*) * pNode->mNumMeshes);

    pObjectOut->buffers_count = pNode->mNumMeshes;

    for (mesh_idx = 0; mesh_idx < pNode->mNumMeshes; mesh_idx++) {
        mesh = pScene->mMeshes[pNode->mMeshes[mesh_idx]];

        struct Vertex* vertices = SDL_malloc(sizeof(struct Vertex) * mesh->mNumVertices);

        /* It's not easy to predict the size of this array beforehand, so we need to dynamically resize this array as needed. */
        Sint32* indices = NULL;

        /* size of the array, grows in increments of 1, and should be multiplied by 32. */
        size_t indices_size = 0;

        /* amount of indices */
        size_t index_count = 0;

        for (size_t vert_idx = 0; vert_idx < mesh->mNumVertices; vert_idx++) {
            vertices[vert_idx].vert[0] = mesh->mVertices[vert_idx].x;
            vertices[vert_idx].vert[1] = mesh->mVertices[vert_idx].y;
            vertices[vert_idx].vert[2] = mesh->mVertices[vert_idx].z;
        }

        for (size_t face_idx = 0; face_idx < mesh->mNumFaces; face_idx++) {
            index_count += mesh->mFaces[face_idx].mNumIndices;

            /* If the index count (including the new indices) outgrows the size of the array. */
            if (index_count > indices_size * 32) {
                /* Calculate "how many 32's can completely cover index_count" */
                size_t new_array_size = (int)SDL_ceilf((index_count + 1) / 32.f);

                /* Create an array with as many 32's as we need to completely cover index_count. */
                Sint32* new_array = SDL_malloc(sizeof(Sint32) * new_array_size * 32);

                if (indices) {
                    SDL_memcpy(new_array, indices, sizeof(Sint32) * indices_size * 32);
                    SDL_free(indices);
                }

                indices = new_array;
                indices_size = new_array_size;
            }

            SDL_memcpy(&indices[index_count - (mesh->mFaces[face_idx].mNumIndices)], mesh->mFaces[face_idx].mIndices, mesh->mFaces[face_idx].mNumIndices * sizeof(Sint32));
        }

        if (!CreateVertexBuffer(vertices, mesh->mNumVertices, &pObjectOut->vertex_buffers[mesh_idx])) {
            return false;
        }

        if (!CreateIndexBuffer(indices, index_count, &pObjectOut->index_buffers[mesh_idx])) {
            return false;
        }
    }

    return true;
}

/* Get objects_array's size, the array is sized at the nearest 32. */
static inline size_t GetObjectsSize() {
    return (int)SDL_ceilf(objects_count / 32.f) * 32;
}

/* Make space for an extra object */
static inline struct Object* EmplaceObject() {
    /* Should we resize the array? True if we surpass a multiple of 32. */
    bool resize_array = objects_count % 33 == 0;

    objects_count++;
    if (resize_array) {
        struct Object** new_array = SDL_malloc(sizeof(struct Object*) * GetObjectsSize());

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

/* Recursively load all the objects in the scene */
static inline bool LoadSceneObjects(const struct aiScene* scene, const struct aiNode* node) {
    if (node->mNumMeshes > 0 && !LoadObject(scene, node, EmplaceObject())) {
        return false;
    }
    for (size_t i = 0; i < node->mNumChildren; i++) {
        if (!LoadSceneObjects(scene, node->mChildren[i])) {
            return false;
        }
    }

    return true;
}

/* Loads all the models and stuff necessary for the game! */
static inline bool LoadScene() {
    const struct aiScene* scene = aiImportFile("models/test.glb", 0);

    if (!scene) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to import 'models/test.glb'!\n");
        return false;
    }

    if (!LoadSceneObjects(scene, scene->mRootNode)) {
        return false;
    }

    return true;
}

static void GoToMainMenu(const ConnectionHandle _, [[gnu::unused]] const char* const _pReason) {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

bool IntroInit(SDL_GPUDevice* pGPUDevice) {
    gpu_device = pGPUDevice;

    if (!InitTestPipeline()) {
        return false;
    }

    NETSetClientDisconnectCallback(GoToMainMenu);

    return true;
}

bool IntroRender(void) {
    /* initialize scene if not initialized, exit on failure */
    if (!objects_array && !LoadScene()) {
        return false;
    }

    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){0.f, 0.f, 0.f, 1.f};
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = LESwapchainTexture;

    static SDL_GPURenderPass* render_pass;

    SDL_GPUViewport viewport = {0, 0, LESwapchainWidth, LESwapchainHeight, 0.0f, 1.0f};

    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, NULL))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, test_pipeline);
    SDL_SetGPUViewport(render_pass, &viewport);

    struct Object* obj;
    for (size_t i = 0; i < objects_count; i++) {
        obj = objects_array[i];

        for (size_t buf_idx = 0; buf_idx < obj->buffers_count; buf_idx++) {
            SDL_GPUBufferBinding vertex_buffer_binding;
            vertex_buffer_binding.buffer = obj->vertex_buffers[buf_idx].buffer;
            vertex_buffer_binding.offset = 0;

            SDL_GPUBufferBinding index_buffer_binding;
            index_buffer_binding.buffer = obj->index_buffers[buf_idx].buffer;
            index_buffer_binding.offset = 0;

            SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_buffer_binding, 1);
            SDL_BindGPUIndexBuffer(render_pass, &index_buffer_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_DrawGPUIndexedPrimitives(render_pass, obj->index_buffers[buf_idx].count, 1, 0, 0, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);

    return true;
}

void IntroCleanup(void) {
    /* TODO: free scene objects */

    SDL_ReleaseGPUGraphicsPipeline(gpu_device, test_pipeline);

    SDL_ReleaseGPUShader(gpu_device, test_shader.vertex);
    SDL_ReleaseGPUShader(gpu_device, test_shader.fragment);
}
