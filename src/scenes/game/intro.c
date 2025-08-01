#include "scenes/game/intro.h"
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
#include <cglm/cam.h>
#include <cglm/cglm.h>
#include <cglm/affine.h>
#include <cglm/quat.h>
#include <cglm/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>

struct Shader {
    SDL_GPUShader *vertex;
    SDL_GPUShader *fragment;
};

struct Vertex {
    vec3 vert;
};

struct Buffer {
    struct SDL_GPUBuffer *buffer;

    /* amount of elements */
    size_t count;
};

/* An object in the game world */
struct Object {
    struct aiVector3D position;
    struct aiQuaternion rotation;
    struct aiVector3D scale;

    struct Buffer *vertex_buffers;
    struct Buffer *index_buffers;

    /* vertex_buffers and index_buffers are the same length */
    size_t buffers_count;
};

struct PlayerObjectList {
    struct PlayerObjectList *prev;

    struct Object *obj;
    int id;

    struct PlayerObjectList *next;
};

static SDL_GPUDevice *gpu_device = NULL;

static struct Shader test_shader;
static SDL_GPUGraphicsPipeline *test_pipeline = NULL;

static struct Object **objects_array = NULL;
static Uint32 objects_count = 0;

static struct PlayerObjectList *player_objects;

static vec3 camera_pos = { 0, 0, 0 };

alignas(16) static struct MatricesUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
} matrices;

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

static inline bool InitTestPipeline() {
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

    if (!LoadShader("shaders/test_shader.vert.spv", (Uint8 **)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size)) {
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

    if (!LoadShader("shaders/test_shader.frag.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
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

    struct SDL_GPUVertexAttribute vertex_attribute;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attribute.location = 0;
    vertex_attribute.offset = 0;

    struct SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.target_info.has_depth_stencil_target = false;
    graphics_pipeline_create_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_A8_UNORM;
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

    pIndexBufferOut->count = indexCount;

    return true;
}

/* Create an Object out of an aiNode */
static inline bool LoadObject(const struct aiScene *pScene, const struct aiNode *pNode, struct Object *pObjectOut) {
    static size_t mesh_idx;
    static struct aiMesh *mesh;

    aiDecomposeMatrix(&pNode->mTransformation, &pObjectOut->scale, &pObjectOut->rotation, &pObjectOut->position);

    pObjectOut->vertex_buffers = SDL_malloc(sizeof(struct Buffer *) * pNode->mNumMeshes);
    pObjectOut->index_buffers = SDL_malloc(sizeof(struct Buffer *) * pNode->mNumMeshes);

    pObjectOut->buffers_count = pNode->mNumMeshes;

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

        if (!CreateVertexBuffer(vertices, mesh->mNumVertices, &pObjectOut->vertex_buffers[mesh_idx])) {
            return false;
        }

        if (!CreateIndexBuffer(indices, index_count, &pObjectOut->index_buffers[mesh_idx])) {
            return false;
        }

        SDL_free(vertices);
        SDL_free(indices);
    }

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
static inline bool LoadSceneObjects(const struct aiScene *scene, const struct aiNode *node) {
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
    const struct aiScene *scene = aiImportFile("models/test.glb", 0);

    if (!scene) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to import 'models/test.glb'!\n");
        return false;
    }

    if (!LoadSceneObjects(scene, scene->mRootNode)) {
        return false;
    }

    aiReleaseImport(scene);

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

        if (!LoadObject(character_scene, character_node, player_obj->obj)) {
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

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "player %d update:\n", player_obj->id);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "\tnew position: %f %f %f\n", player->position[0], player->position[1], player->position[2]);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "\tnew rotation: %f %f %f %f\n", player->rotation[0], player->rotation[1], player->rotation[2], player->rotation[3]);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "\tnew scale: %f %f %f\n", player->scale[0], player->scale[1], player->scale[2]);
}

bool IntroInit(SDL_GPUDevice *pGPUDevice) {
    gpu_device = pGPUDevice;

    glm_mat4_identity(matrices.view);
    glm_mat4_identity(matrices.projection);

    //glm_lookat(camera_pos, (vec3){0, 0, 0}, (vec3){0, 1, 0}, matrices.view);

    if (!InitTestPipeline()) {
        return false;
    }

    NETSetClientDisconnectCallback(GoToMainMenu);
    NETSetClientUpdateCallback(OnPlayerUpdate);

    return true;
}

bool IntroRender(void) {
    /* initialize scene if not initialized, exit on failure */
    if (!objects_array && !LoadScene()) {
        return false;
    }

    glm_perspective(1.5708f, (float)LESwapchainWidth/(float)LESwapchainHeight, 0.1f, 1000.f, matrices.projection);
    glm_look(camera_pos, (vec3){-1, 0, 0}, (vec3){0, 1, 0}, matrices.view);

    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){0.f, 0.f, 0.f, 1.f};
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = LESwapchainTexture;

    static SDL_GPURenderPass *render_pass;

    SDL_GPUViewport viewport = {0, 0, LESwapchainWidth, LESwapchainHeight, 0.0f, 1.0f};

    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, NULL))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, test_pipeline);
    SDL_SetGPUViewport(render_pass, &viewport);

    struct Object *obj;
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

            glm_mat4_identity(matrices.model);

            glm_translate(matrices.model, (vec3){ obj->position.x, obj->position.y, obj->position.z });
            glm_quat_rotate(matrices.model, (vec4){ obj->rotation.x, obj->rotation.y, obj->rotation.z, obj->rotation.w }, matrices.model);
            glm_scale(matrices.model, (vec3){ obj->scale.x, obj->scale.y, obj->scale.z });

            SDL_PushGPUVertexUniformData(LECommandBuffer, 0, &matrices, sizeof(matrices));

            SDL_DrawGPUIndexedPrimitives(render_pass, obj->index_buffers[buf_idx].count, 1, 0, 0, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);

    return true;
}

void IntroCleanup(void) {
    /* TODO: free scene objects */
    for (; objects_count > 0; objects_count--) {
        struct Object *object = objects_array[objects_count - 1];

        for (; object->buffers_count > 0; object->buffers_count--) {
            SDL_ReleaseGPUBuffer(gpu_device, object->vertex_buffers[object->buffers_count - 1].buffer);
            SDL_ReleaseGPUBuffer(gpu_device, object->index_buffers[object->buffers_count - 1].buffer);
        }
        SDL_free(object->vertex_buffers);
        SDL_free(object->index_buffers);

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

    SDL_ReleaseGPUGraphicsPipeline(gpu_device, test_pipeline);

    SDL_ReleaseGPUShader(gpu_device, test_shader.vertex);
    SDL_ReleaseGPUShader(gpu_device, test_shader.fragment);
}
