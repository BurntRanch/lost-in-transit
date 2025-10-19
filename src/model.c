#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_image/SDL_image.h>
#include <assimp/cimport.h>
#include <cglm/mat4.h>
#include "assimp/scene.h"
#include "engine.h"

#include "model.h"

static struct GraphicsPipeline textured_cel_shader = {NULL, NULL, NULL};
static struct GraphicsPipeline untextured_cel_shader = {NULL, NULL, NULL};

struct LightUBO MLLightUBO = {0};

static inline void aiVector3ToVec3(struct aiVector3D *src, float *dst) {
    dst[0] = src->x;
    dst[1] = src->y;
    dst[2] = src->z;
}

static inline void aiVector2ToVec2(struct aiVector2D *src, float *dst) {
    dst[0] = src->x;
    dst[1] = src->y;
}

static inline void aiQuaternionToVec4(struct aiQuaternion *src, float *dst) {
    dst[0] = src->x;
    dst[1] = src->y;
    dst[2] = src->z;
    dst[3] = src->w;
}

static inline void aiMatrix4ToMat4(vec4 *dst, const struct aiMatrix4x4 *src) {
    dst[0][0] = src->a1;
    dst[0][1] = src->b1;
    dst[0][2] = src->c1;
    dst[0][3] = src->d1;

    dst[1][0] = src->a2;
    dst[1][1] = src->b2;
    dst[1][2] = src->c2;
    dst[1][3] = src->d2;

    dst[2][0] = src->a3;
    dst[2][1] = src->b3;
    dst[2][2] = src->c3;
    dst[2][3] = src->d3;

    dst[3][0] = src->a4;
    dst[3][1] = src->b4;
    dst[3][2] = src->c4;
    dst[3][3] = src->d4;
}

static inline bool CopySurfaceToTexture(SDL_Surface *surface, SDL_GPUTexture *texture, SDL_GPUDevice *gpu_device) {
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

static inline bool CreateVertexBuffer(const struct Vertex *pVertices, size_t vertexCount, struct Buffer *pVertexBufferOut, SDL_GPUDevice *gpu_device) {
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

static inline bool CreateIndexBuffer(const Sint32 *pIndices, size_t indexCount, struct Buffer *pIndexBufferOut, SDL_GPUDevice *gpu_device) {
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

size_t MLFindBoneByName(const struct Model *pModel, const char *name) {
    for (size_t bone_idx = 0; bone_idx < pModel->bone_count; bone_idx++) {
        if (SDL_strcmp(pModel->bones[bone_idx].name, name) == 0) {
            return bone_idx;
        }
    }

    return -1;
}

/* Create an Object out of an aiNode */
static inline bool LoadObject(const struct aiScene *pScene, struct Model *scene, const struct aiNode *pNode, struct Object *pObjectOut, struct Object *pParent) {
    static size_t mesh_idx;
    static struct aiMesh *mesh;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading object %s! (child of %s).\n", pNode->mName.data, (pParent ? pParent->name : "--none--"));

    pObjectOut->name = SDL_malloc(pNode->mName.length + 1);
    SDL_memcpy(pObjectOut->name, pNode->mName.data, pNode->mName.length + 1);

    static struct aiVector3D pos_vec3D;
    static struct aiQuaternion rot_quat;
    static struct aiVector3D sca_vec3D;

    aiDecomposeMatrix(&pNode->mTransformation, &sca_vec3D, &rot_quat, &pos_vec3D);

    aiVector3ToVec3(&pos_vec3D, pObjectOut->position);
    aiQuaternionToVec4(&rot_quat, pObjectOut->rotation);
    aiVector3ToVec3(&sca_vec3D, pObjectOut->scale);

    pObjectOut->meshes = SDL_malloc(sizeof(struct Mesh) * pNode->mNumMeshes);
    pObjectOut->mesh_count = pNode->mNumMeshes;

    SDL_GPUDevice *gpu_device = LEGetGPUDevice();

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
            aiVector3ToVec3(&mesh->mVertices[vert_idx], vertices[vert_idx].vert);
            aiVector2ToVec2((struct aiVector2D *)(&mesh->mTextureCoords[0][vert_idx]), vertices[vert_idx].uv);
            aiVector3ToVec3(&mesh->mNormals[vert_idx], vertices[vert_idx].norm);

            vertices[vert_idx].bone_ids[0] = -1;
            vertices[vert_idx].bone_ids[1] = -1;
            vertices[vert_idx].bone_ids[2] = -1;
            vertices[vert_idx].bone_ids[3] = -1;
        }

        for (size_t bone_idx = 0; bone_idx < mesh->mNumBones; bone_idx++) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing bone '%s'\n", mesh->mBones[bone_idx]->mName.data);

            struct aiBone *bone = mesh->mBones[bone_idx];
            size_t bone_id = MLFindBoneByName(scene, bone->mName.data);
            if (bone_id == (size_t)-1) {
                scene->bones[bone_id = scene->bone_count++].name = SDL_malloc(bone->mName.length + 1);
                SDL_memcpy(scene->bones[bone_id].name, bone->mName.data, bone->mName.length + 1);

                scene->bones[bone_id].position_key_count = 0;
                scene->bones[bone_id].rotation_key_count = 0;
                scene->bones[bone_id].scale_key_count = 0;
            }

            aiMatrix4ToMat4(scene->bones[bone_id].offset_matrix, &bone->mOffsetMatrix);
            glm_mat4_inv(scene->bones[bone_id].offset_matrix, scene->bones[bone_id].offset_matrix_inv);
            aiMatrix4ToMat4(scene->bones[bone_id].local_transform, &pNode->mTransformation);

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

        if (!CreateVertexBuffer(vertices, mesh->mNumVertices, &pObjectOut->meshes[mesh_idx].vertex_buffer, gpu_device)) {
            return false;
        }

        if (!CreateIndexBuffer(indices, index_count, &pObjectOut->meshes[mesh_idx].index_buffer, gpu_device)) {
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
    
            if (!textured_cel_shader.graphics_pipeline && !LEInitPipeline(&textured_cel_shader, PIPELINE_VERTEX_DEFAULT | PIPELINE_FRAG_TEXTURED_CEL)) {
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

            if (!CopySurfaceToTexture(texture_surface, pObjectOut->meshes[mesh_idx].texture.gpu_texture, gpu_device)) {
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

            pObjectOut->meshes[mesh_idx].pipeline = &textured_cel_shader;
        } else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "diffuse texture not found, using untextured shader!\n");
            pObjectOut->meshes[mesh_idx].texture.gpu_sampler = NULL;
            pObjectOut->meshes[mesh_idx].texture.gpu_texture = NULL;
            
            if (!untextured_cel_shader.graphics_pipeline && !LEInitPipeline(&untextured_cel_shader, PIPELINE_VERTEX_DEFAULT | PIPELINE_FRAG_UNTEXTURED_CEL)) {
                return false;
            }

            pObjectOut->meshes[mesh_idx].pipeline = &untextured_cel_shader;
        }

        SDL_free(vertices);
        SDL_free(indices);
    }

    pObjectOut->parent = pParent;

    return true;
}

/* Make space for an extra object */
static inline struct Object *EmplaceObject(struct Model *pScene) {
    /* Should we resize the array? True if we surpass a multiple of 32. */
    bool resize_array = pScene->object_count % 33 == 0;

    size_t old_object_size = (size_t)SDL_ceilf(pScene->object_count / 32.f) * 32;
    size_t new_object_size = (size_t)SDL_ceilf((pScene->object_count + 1) / 32.f) * 32;

    pScene->object_count++;
    if (resize_array) {
        struct Object *new_array = SDL_malloc(sizeof(struct Object) * new_object_size);

        if (pScene->objects) {
            SDL_memcpy(new_array, pScene->objects, old_object_size);
            SDL_free(pScene->objects);
        }

        pScene->objects = new_array;
    }

    return &pScene->objects[pScene->object_count - 1];
}

/* Recursively load all the objects in the scene starting from node (and its children) */
static bool LoadSceneObjects(const struct aiScene *aiScene, struct Model *scene, const struct aiNode *node, struct Object *parent) {
    struct Object *object = EmplaceObject(scene);
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

struct Model *MLImportModel(const char * const filename) {
    const struct aiScene *aiScene = aiImportFile(filename, 0);
    
    struct Model *model = SDL_malloc(sizeof(struct Model));
    model->bone_count = 0;
    model->animation_playing = false;
    model->current_animation.duration = 0;

    model->object_count = 0;
    model->objects = NULL;

    if (!aiScene) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to import model '%s'!\n", filename);
        return NULL;
    }

    if (aiScene->mNumAnimations > 0) {
        struct aiAnimation *animation = aiScene->mAnimations[0];

        model->animation_playing = true;
        model->animation_time = 0.0;
        model->current_animation.duration = animation->mDuration;
        model->current_animation.ticks_per_sec = animation->mTicksPerSecond;
        
        for (size_t channel_idx = 0; channel_idx < animation->mNumChannels; channel_idx++) {
            struct aiNodeAnim *channel = animation->mChannels[channel_idx];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing channel '%s'!\n", channel->mNodeName.data);

            model->bones[model->bone_count].name = SDL_malloc(channel->mNodeName.length + 1);
            SDL_memcpy(model->bones[model->bone_count].name, channel->mNodeName.data, channel->mNodeName.length + 1);

            model->bones[model->bone_count].position_key_count = channel->mNumPositionKeys;
            model->bones[model->bone_count].position_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * model->bones[model->bone_count].position_key_count);
            for (size_t position_key_idx = 0; position_key_idx < channel->mNumPositionKeys; position_key_idx++) {
                model->bones[model->bone_count].position_keys[position_key_idx].value[0] = channel->mPositionKeys[position_key_idx].mValue.x;
                model->bones[model->bone_count].position_keys[position_key_idx].value[1] = channel->mPositionKeys[position_key_idx].mValue.y;
                model->bones[model->bone_count].position_keys[position_key_idx].value[2] = channel->mPositionKeys[position_key_idx].mValue.z;
                model->bones[model->bone_count].position_keys[position_key_idx].timestamp = channel->mPositionKeys[position_key_idx].mTime;
            }

            model->bones[model->bone_count].rotation_key_count = channel->mNumRotationKeys;
            model->bones[model->bone_count].rotation_keys = SDL_malloc(sizeof(struct QuatKeyframe) * model->bones[model->bone_count].rotation_key_count);
            for (size_t rotation_key_idx = 0; rotation_key_idx < channel->mNumRotationKeys; rotation_key_idx++) {
                model->bones[model->bone_count].rotation_keys[rotation_key_idx].value[0] = channel->mRotationKeys[rotation_key_idx].mValue.x;
                model->bones[model->bone_count].rotation_keys[rotation_key_idx].value[1] = channel->mRotationKeys[rotation_key_idx].mValue.y;
                model->bones[model->bone_count].rotation_keys[rotation_key_idx].value[2] = channel->mRotationKeys[rotation_key_idx].mValue.z;
                model->bones[model->bone_count].rotation_keys[rotation_key_idx].value[3] = channel->mRotationKeys[rotation_key_idx].mValue.w;
                model->bones[model->bone_count].rotation_keys[rotation_key_idx].timestamp = channel->mRotationKeys[rotation_key_idx].mTime;
            }

            model->bones[model->bone_count].scale_key_count = channel->mNumScalingKeys;
            model->bones[model->bone_count].scale_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * model->bones[model->bone_count].scale_key_count);
            for (size_t scale_key_idx = 0; scale_key_idx < channel->mNumScalingKeys; scale_key_idx++) {
                model->bones[model->bone_count].scale_keys[scale_key_idx].value[0] = channel->mScalingKeys[scale_key_idx].mValue.x;
                model->bones[model->bone_count].scale_keys[scale_key_idx].value[1] = channel->mScalingKeys[scale_key_idx].mValue.y;
                model->bones[model->bone_count].scale_keys[scale_key_idx].value[2] = channel->mScalingKeys[scale_key_idx].mValue.z;
                model->bones[model->bone_count].scale_keys[scale_key_idx].timestamp = channel->mScalingKeys[scale_key_idx].mTime;
            }

            model->bone_count++;
        }
    }

    if (!LoadSceneObjects(aiScene, model, aiScene->mRootNode, NULL)) {
        return NULL;
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

        aiVector3ToVec3(&position, MLLightUBO.lights[MLLightUBO.lights_count].pos);
        aiVector3ToVec3(&diffuse, MLLightUBO.lights[MLLightUBO.lights_count].diffuse);
        aiVector3ToVec3(&specular, MLLightUBO.lights[MLLightUBO.lights_count].specular);
        aiVector3ToVec3(&ambient, MLLightUBO.lights[MLLightUBO.lights_count].ambient);

        MLLightUBO.lights[MLLightUBO.lights_count].scene_ptr = (Uint64)model;

        MLLightUBO.lights_count++;
        SDL_assert(MLLightUBO.lights_count < 256);
    }

    aiReleaseImport(aiScene);

    return model;
}

void MLDestroyModel(struct Model *pModel) {
    for (; pModel->bone_count > 0; pModel->bone_count--) {
        struct Bone *bone = &pModel->bones[pModel->bone_count - 1];

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

    SDL_GPUDevice *gpu_device = LEGetGPUDevice();

    for (; pModel->object_count > 0; pModel->object_count--) {
        struct Object *object = &pModel->objects[pModel->object_count - 1];

        if (object->name) {
            SDL_free(object->name);
        }

        for (; object->mesh_count > 0; object->mesh_count--) {
            SDL_ReleaseGPUBuffer(gpu_device, object->meshes[object->mesh_count - 1].vertex_buffer.buffer);
            SDL_ReleaseGPUBuffer(gpu_device, object->meshes[object->mesh_count - 1].index_buffer.buffer);

            if (object->meshes[object->mesh_count - 1].texture.gpu_sampler && object->meshes[object->mesh_count - 1].texture.gpu_texture) {
                SDL_ReleaseGPUSampler(gpu_device, object->meshes[object->mesh_count - 1].texture.gpu_sampler);
                SDL_ReleaseGPUTexture(gpu_device, object->meshes[object->mesh_count - 1].texture.gpu_texture);
            }
        }

        SDL_free(object->meshes);
    }
    if (pModel->objects) {
        SDL_free(pModel->objects);
    }

    /* loop through the lights and remove any lights imported from this scene */
    int i;
    for (i = 0; i < MLLightUBO.lights_count;) {
        if (MLLightUBO.lights[i].scene_ptr != (Uint64)pModel) {
            i++;
            continue;
        }
        SDL_memmove(&MLLightUBO.lights[i], &MLLightUBO.lights[i + 1], (MLLightUBO.lights_count - (i + 1)) * sizeof(struct Light));
        MLLightUBO.lights_count--;
    }

    SDL_free(pModel);
}
