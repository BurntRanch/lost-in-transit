#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/vector3.h"
#include "button.h"
#include "options.h"
#include "scenes/game/intro.h"
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3_image/SDL_image.h>
#include <cglm/affine.h>
#include <cglm/cam.h>
#include <cglm/mat4.h>
#include <cglm/quat.h>
#define TITLE "Lost In Transit"

#include "engine.h"
#include "scenes.h"
#include "label.h"

#include "scenes/main_menu.h"
#include "scenes/options.h"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>

#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>
#include <time.h>

enum Shaders {
    UNTEXTURED_TEST_SHADER,
    TEXTURED_TEST_SHADER,
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
    /* which shader should we use to render this? */
    enum Shaders shader;

    struct Texture texture;

    struct Material material;

    struct Buffer vertex_buffer;
    struct Buffer index_buffer;
};

static struct LightUBO {
    int lights_count; // 4 bytes
    char pad1[12];  // 12 + 4 bytes

    struct Light lights[256]; // starts at 16 bytes
} light_ubo = {0};

struct Scene3D {
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

struct Shader {
    SDL_GPUShader *vertex;
    SDL_GPUShader *fragment;
};

alignas(16) static struct MatricesUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 bone_matrices[100];
} matrices;

TTF_Font *pLEGameFont = NULL;

/* Resolution defaults. */
int LEScreenWidth = 800;
int LEScreenHeight = 600;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_GPUDevice *gpu_device = NULL;
SDL_GPUCommandBuffer *LECommandBuffer = NULL;

static SDL_Texture *render_texture = NULL;

/* we keep this open because why should we keep opening/closing it each frame? */
static SDL_GPUTransferBuffer *render_transferbuffer = NULL;

static struct Shader untextured_test_shader;
static struct Shader textured_test_shader;
static SDL_GPUGraphicsPipeline *untextured_test_pipeline = NULL;
static SDL_GPUGraphicsPipeline *textured_test_pipeline = NULL;
static struct SDL_GPURenderPass *render_pass = NULL;
static struct RenderInfo render_info;

static struct SceneTransition {
    enum Scene dest;
    /* how far we're in the transition */
    float perc;

    bool active;
} scene_transition;

static bool InitGPURenderTexture(void) {
    if (LESwapchainTexture) {
        SDL_ReleaseGPUTexture(gpu_device, LESwapchainTexture);
        LESwapchainTexture = NULL;
    }

    if (LEDepthStencilTexture) {
        SDL_ReleaseGPUTexture(gpu_device, LEDepthStencilTexture);
        LEDepthStencilTexture = NULL;
    }

    if (render_transferbuffer) {
        SDL_ReleaseGPUTransferBuffer(gpu_device, render_transferbuffer);
        render_transferbuffer = NULL;
    }

    if (render_texture) {
        SDL_DestroyTexture(render_texture);
        render_texture = NULL;
    }

    static SDL_GPUTextureCreateInfo gpu_texture_create_info;
    gpu_texture_create_info.type = SDL_GPU_TEXTURETYPE_2D;
    gpu_texture_create_info.props = 0;
    gpu_texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    gpu_texture_create_info.width = (LESwapchainWidth = LEScreenWidth);
    gpu_texture_create_info.height = (LESwapchainHeight = LEScreenHeight);
    gpu_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gpu_texture_create_info.num_levels = 1;
    gpu_texture_create_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpu_texture_create_info.layer_count_or_depth = 1;

    static SDL_GPUTextureCreateInfo depth_stencil_texture_create_info;
    depth_stencil_texture_create_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_stencil_texture_create_info.props = 0;
    depth_stencil_texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_stencil_texture_create_info.width = (LESwapchainWidth = LEScreenWidth);
    depth_stencil_texture_create_info.height = (LESwapchainHeight = LEScreenHeight);
    depth_stencil_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    depth_stencil_texture_create_info.num_levels = 1;
    depth_stencil_texture_create_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    depth_stencil_texture_create_info.layer_count_or_depth = 1;
    
    /* TODO: This keeps creating file descriptors, and doesn't close them. why? */
    if (!(LESwapchainTexture = SDL_CreateGPUTexture(gpu_device, &gpu_texture_create_info))) {
        fprintf(stderr, "Failed to create GPU texture! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(LEDepthStencilTexture = SDL_CreateGPUTexture(gpu_device, &depth_stencil_texture_create_info))) {
        fprintf(stderr, "Failed to create GPU texture! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = LESwapchainWidth * LESwapchainHeight * 4;  /* 4 bytes for each pixel */

    if (!(render_transferbuffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
        fprintf(stderr, "Failed to create GPU Transfer buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    /* the colors are reversed so this is effectively RGBA. Please don't ask me anything about this. */
    if (!(render_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, LESwapchainWidth, LESwapchainHeight))) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to create render_texture! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void LEDestroyGPU(void) {
    if (!gpu_device) {
        return;
    }

    if (LESwapchainTexture) {
        SDL_ReleaseGPUTexture(gpu_device, LESwapchainTexture);
        LESwapchainTexture = NULL;
    }
    if (LEDepthStencilTexture) {
        SDL_ReleaseGPUTexture(gpu_device, LEDepthStencilTexture);
        LEDepthStencilTexture = NULL;
    }
    if (render_transferbuffer) {
        SDL_ReleaseGPUTransferBuffer(gpu_device, render_transferbuffer);
        render_transferbuffer = NULL;
    }

    SDL_DestroyGPUDevice(gpu_device);
    gpu_device = NULL;
}

void LEDestroyWindow(void) {
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;

        /* we have to set the other values to NULL because they're implicitly destroyed */
        renderer = NULL;
        render_texture = NULL;
    }
}

void LEApplySettings(void) {
    SDL_SetRenderVSync(renderer, options.vsync ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED);
}

bool LEInitWindow(void) {
    LEDestroyWindow();

    LEDestroyGPU();

    if (LECommandBuffer) {
        LECommandBuffer = NULL;
    }

    if (!(window = SDL_CreateWindow(TITLE, LEScreenWidth, LEScreenHeight, SDL_WINDOW_VULKAN))) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 400, 300);

    if (!(renderer = SDL_CreateRenderer(window, "opengl"))) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Something went wrong while getting the renderer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (!gpu_device && !(gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU Device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!InitGPURenderTexture()) {
        return false;
    }

    LEApplySettings();

    return true;
}

bool LEInitTTF(void) {
    if (!TTF_Init()) {
        fprintf(stderr, "Something went wrong while initializing SDL3_TTF! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void DestroyText(struct LE_Label *const pLEText) {
    if (pLEText->texture) {
        SDL_DestroyTexture(pLEText->texture);
        pLEText->texture = NULL;
    }

    if (pLEText->surface) {
        SDL_DestroySurface(pLEText->surface);
        pLEText->surface = NULL;
    }
}

bool UpdateText(struct LE_Label *const pLEText) {
    DestroyText(pLEText);

    if (!(pLEText->surface = TTF_RenderText_Blended_Wrapped(pLEGameFont, pLEText->text, 0, (SDL_Color){255, 255, 255, SDL_ALPHA_OPAQUE}, 0))) {
        fprintf(stderr, "Failed to render text! (text: %s) (SDL Error Code: %s)\n", pLEText->text, SDL_GetError());
        return false;
    }
    if (!(pLEText->texture = SDL_CreateTextureFromSurface(renderer, pLEText->surface))) {
        fprintf(stderr, "Failed to create texture from text surface! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

/* What's the currently loaded scene? Refer to scenes.h for values */
static enum Scene scene_loaded = SCENE_NONE;

bool LEPrepareGPURendering(void) {
    if (!(LECommandBuffer = SDL_AcquireGPUCommandBuffer(gpu_device))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire command buffer for GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    render_pass = NULL;

    return true;
}

bool LEStartGPURender(void) {
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

    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, &depth_stencil_target_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_SetGPUViewport(render_pass, &render_info.viewport);

    return true;
}

bool LEFinishGPURendering(void) {
    if (!LECommandBuffer) {
        return false;
    }
    
    if (render_pass)  {
        SDL_EndGPURenderPass(render_pass);
    }

    static SDL_GPUCopyPass *copy_pass;

    if (!(copy_pass = SDL_BeginGPUCopyPass(LECommandBuffer))) {
        fprintf(stderr, "Failed to begin copy pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static SDL_GPUTextureRegion src;
    src.x = 0;
    src.y = 0;
    src.w = LESwapchainWidth;
    src.h = LESwapchainHeight;
    src.z = 0;
    src.d = 1;
    src.layer = 0;
    src.texture = LESwapchainTexture;
    src.mip_level = 0;

    static SDL_GPUTextureTransferInfo texture_transfer_info;
    texture_transfer_info.offset = 0;
    texture_transfer_info.pixels_per_row = LESwapchainWidth;
    texture_transfer_info.rows_per_layer = LESwapchainHeight;
    texture_transfer_info.transfer_buffer = render_transferbuffer;

    SDL_DownloadFromGPUTexture(copy_pass, &src, &texture_transfer_info);

    SDL_EndGPUCopyPass(copy_pass);

    static SDL_GPUFence *fence;
    if (!(fence = SDL_SubmitGPUCommandBufferAndAcquireFence(LECommandBuffer))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to submit command buffer to GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_WaitForGPUFences(gpu_device, 1, &fence, 1);
    SDL_ReleaseGPUFence(gpu_device, fence);

    static void *pixels;
    if (!(pixels = SDL_MapGPUTransferBuffer(gpu_device, render_transferbuffer, false))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to map GPU Transfer buffer to memory! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static void *dst_pixels;
    static int pitch;
    
    if (!SDL_LockTexture(render_texture, NULL, &dst_pixels, &pitch)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to lock render_texture! If this happens too often, please report this issue! (SDL Error: %s)\n", SDL_GetError()); 
        return true;
    }

    SDL_memcpy(dst_pixels, pixels, pitch * LESwapchainHeight);

    SDL_UnmapGPUTransferBuffer(gpu_device, render_transferbuffer);
    SDL_UnlockTexture(render_texture);

    if (!SDL_RenderTexture(renderer, render_texture, NULL, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to display GPU texture to screen! (SDL Error: '%s')\n", SDL_GetError());
        return false;
    }

    return true;
}

bool InitCurrentScene() {
    switch (scene_loaded) {
        case SCENE_MAINMENU:
            if (!MainMenuInit(renderer)) {
                return false;
            }
            break;
        case SCENE_OPTIONS:
            if (!OptionsInit(renderer)) {
                return false;
            }
            break;
        case SCENE3D_INTRO:
            if (!IntroInit(gpu_device)) {
                return false;
            }
            break;
        default:;
    }

    return true;
}

void LELoadScene(const Uint8 scene) {
    scene_transition.dest = scene;
    scene_transition.perc = 0.f;
    scene_transition.active = true;
}

static void aiVector3ToVec3(struct aiVector3D *src, float *dst) {
    dst[0] = src->x;
    dst[1] = src->y;
    dst[2] = src->z;
}

static void aiVector2ToVec2(struct aiVector2D *src, float *dst) {
    dst[0] = src->x;
    dst[1] = src->y;
}

static void aiQuaternionToVec4(struct aiQuaternion *src, float *dst) {
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

/* Make space for an extra object */
static inline struct Object *EmplaceObject(struct Scene3D *pScene) {
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

/* returns index to scene->bones, returns -1 on fail (wraps around to size_t max) */
static inline size_t FindBoneByName(const struct Scene3D *scene, const char *name) {
    for (size_t bone_idx = 0; bone_idx < scene->bone_count; bone_idx++) {
        if (SDL_strcmp(scene->bones[bone_idx].name, name) == 0) {
            return bone_idx;
        }
    }

    return -1;
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

static inline bool InitTexturedTestPipeline(void) {
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

static inline bool InitUntexturedTestPipeline(void) {
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

/* Create an Object out of an aiNode */
static inline bool LoadObject(const struct aiScene *pScene, struct Scene3D *scene, const struct aiNode *pNode, struct Object *pObjectOut, struct Object *pParent) {
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
            size_t bone_id = FindBoneByName(scene, bone->mName.data);
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

    pObjectOut->parent = pParent;

    return true;
}

/* Recursively load all the objects in the scene starting from node (and its children) */
static inline bool LoadSceneObjects(const struct aiScene *aiScene, struct Scene3D *scene, const struct aiNode *node, struct Object *parent) {
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

struct Scene3D *LEImportScene3D(const char * const filename) {
    const struct aiScene *aiScene = aiImportFile(filename, 0);
    
    struct Scene3D *scene3d = SDL_malloc(sizeof(struct Scene3D));
    scene3d->bone_count = 0;
    scene3d->animation_playing = false;
    scene3d->current_animation.duration = 0;

    scene3d->object_count = 0;
    scene3d->objects = NULL;

    if (!aiScene) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to import 3D scene '%s'!\n", filename);
        return NULL;
    }

    if (aiScene->mNumAnimations > 0) {
        struct aiAnimation *animation = aiScene->mAnimations[0];

        scene3d->animation_playing = true;
        scene3d->animation_time = 0.0;
        scene3d->current_animation.duration = animation->mDuration;
        scene3d->current_animation.ticks_per_sec = animation->mTicksPerSecond;
        
        for (size_t channel_idx = 0; channel_idx < animation->mNumChannels; channel_idx++) {
            struct aiNodeAnim *channel = animation->mChannels[channel_idx];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing channel '%s'!\n", channel->mNodeName.data);

            scene3d->bones[scene3d->bone_count].name = SDL_malloc(channel->mNodeName.length + 1);
            SDL_memcpy(scene3d->bones[scene3d->bone_count].name, channel->mNodeName.data, channel->mNodeName.length + 1);

            scene3d->bones[scene3d->bone_count].position_key_count = channel->mNumPositionKeys;
            scene3d->bones[scene3d->bone_count].position_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * scene3d->bones[scene3d->bone_count].position_key_count);
            for (size_t position_key_idx = 0; position_key_idx < channel->mNumPositionKeys; position_key_idx++) {
                scene3d->bones[scene3d->bone_count].position_keys[position_key_idx].value[0] = channel->mPositionKeys[position_key_idx].mValue.x;
                scene3d->bones[scene3d->bone_count].position_keys[position_key_idx].value[1] = channel->mPositionKeys[position_key_idx].mValue.y;
                scene3d->bones[scene3d->bone_count].position_keys[position_key_idx].value[2] = channel->mPositionKeys[position_key_idx].mValue.z;
                scene3d->bones[scene3d->bone_count].position_keys[position_key_idx].timestamp = channel->mPositionKeys[position_key_idx].mTime;
            }

            scene3d->bones[scene3d->bone_count].rotation_key_count = channel->mNumRotationKeys;
            scene3d->bones[scene3d->bone_count].rotation_keys = SDL_malloc(sizeof(struct QuatKeyframe) * scene3d->bones[scene3d->bone_count].rotation_key_count);
            for (size_t rotation_key_idx = 0; rotation_key_idx < channel->mNumRotationKeys; rotation_key_idx++) {
                scene3d->bones[scene3d->bone_count].rotation_keys[rotation_key_idx].value[0] = channel->mRotationKeys[rotation_key_idx].mValue.x;
                scene3d->bones[scene3d->bone_count].rotation_keys[rotation_key_idx].value[1] = channel->mRotationKeys[rotation_key_idx].mValue.y;
                scene3d->bones[scene3d->bone_count].rotation_keys[rotation_key_idx].value[2] = channel->mRotationKeys[rotation_key_idx].mValue.z;
                scene3d->bones[scene3d->bone_count].rotation_keys[rotation_key_idx].value[3] = channel->mRotationKeys[rotation_key_idx].mValue.w;
                scene3d->bones[scene3d->bone_count].rotation_keys[rotation_key_idx].timestamp = channel->mRotationKeys[rotation_key_idx].mTime;
            }

            scene3d->bones[scene3d->bone_count].scale_key_count = channel->mNumScalingKeys;
            scene3d->bones[scene3d->bone_count].scale_keys = SDL_malloc(sizeof(struct Vec3Keyframe) * scene3d->bones[scene3d->bone_count].scale_key_count);
            for (size_t scale_key_idx = 0; scale_key_idx < channel->mNumScalingKeys; scale_key_idx++) {
                scene3d->bones[scene3d->bone_count].scale_keys[scale_key_idx].value[0] = channel->mScalingKeys[scale_key_idx].mValue.x;
                scene3d->bones[scene3d->bone_count].scale_keys[scale_key_idx].value[1] = channel->mScalingKeys[scale_key_idx].mValue.y;
                scene3d->bones[scene3d->bone_count].scale_keys[scale_key_idx].value[2] = channel->mScalingKeys[scale_key_idx].mValue.z;
                scene3d->bones[scene3d->bone_count].scale_keys[scale_key_idx].timestamp = channel->mScalingKeys[scale_key_idx].mTime;
            }

            scene3d->bone_count++;
        }
    }

    if (!LoadSceneObjects(aiScene, scene3d, aiScene->mRootNode, NULL)) {
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

        aiVector3ToVec3(&position, light_ubo.lights[light_ubo.lights_count].pos);
        aiVector3ToVec3(&diffuse, light_ubo.lights[light_ubo.lights_count].diffuse);
        aiVector3ToVec3(&specular, light_ubo.lights[light_ubo.lights_count].specular);
        aiVector3ToVec3(&ambient, light_ubo.lights[light_ubo.lights_count].ambient);

        light_ubo.lights[light_ubo.lights_count].scene_ptr = (Uint64)scene3d;

        light_ubo.lights_count++;
        SDL_assert(light_ubo.lights_count < 256);
    }

    aiReleaseImport(aiScene);

    return scene3d;
}

struct Object *LEGetSceneObjects(const struct Scene3D * const pScene3D, size_t * const pCountOut) {
    if (pCountOut) {
        *pCountOut = pScene3D->object_count;
    }
    return pScene3D->objects;
}

struct RenderInfo *LEGetRenderInfo(void) {
    return &render_info;
}

static inline void StepAnimation(struct Scene3D *pScene3D) {
    if (pScene3D->animation_playing) {
        pScene3D->animation_time += LEFrametime * pScene3D->current_animation.ticks_per_sec;
        
        if (pScene3D->animation_time >= pScene3D->current_animation.duration) {
            pScene3D->animation_time = SDL_fmod(pScene3D->animation_time, pScene3D->current_animation.duration);
        }

        /* update all bone local transforms */
        for (size_t bone_idx = 0; bone_idx < pScene3D->bone_count; bone_idx++) {
            vec3 position = {0, 0, 0};
            vec3 scale = {1, 1, 1};
            vec4 rotation = {0, 0, 0, 1};

            struct Bone *bone = &pScene3D->bones[bone_idx];

            size_t key_idx = 0;
            for (key_idx = 0; key_idx < bone->position_key_count; key_idx++) {
                if (bone->position_keys[key_idx].timestamp < pScene3D->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(position, bone->position_keys->value, sizeof(position));
                    break;
                }

                double last_timestamp = bone->position_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->position_keys[key_idx].timestamp;

                glm_vec3_lerp(bone->position_keys[key_idx - 1].value, bone->position_keys[key_idx].value, (pScene3D->animation_time - last_timestamp) / (new_timestamp - last_timestamp), position);
                //aiVector3Lerp(&bone->position_keys[key_idx - 1].value, &bone->position_keys[key_idx].value, (scene->animation_time - last_timestamp) / (new_timestamp - last_timestamp), &position);
                break;
            }

            for (key_idx = 0; key_idx < bone->rotation_key_count; key_idx++) {
                if (bone->rotation_keys[key_idx].timestamp < pScene3D->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(rotation, bone->rotation_keys->value, sizeof(rotation));
                    break;
                }

                double last_timestamp = bone->rotation_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->rotation_keys[key_idx].timestamp;

                glm_quat_slerp(bone->rotation_keys[key_idx - 1].value, bone->rotation_keys[key_idx].value, (pScene3D->animation_time - last_timestamp) / (new_timestamp - last_timestamp), rotation);
                break;
            }

            for (key_idx = 0; key_idx < bone->scale_key_count; key_idx++) {
                if (bone->scale_keys[key_idx].timestamp < pScene3D->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(scale, bone->scale_keys->value, sizeof(scale));
                    break;
                }

                double last_timestamp = bone->scale_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->scale_keys[key_idx].timestamp;

                glm_vec3_lerp(bone->scale_keys[key_idx - 1].value, bone->scale_keys[key_idx].value, (pScene3D->animation_time - last_timestamp) / (new_timestamp - last_timestamp), scale);
                break;
            }
            
            glm_mat4_identity(bone->local_transform);
            glm_translate(bone->local_transform, position);
            glm_quat_rotate(bone->local_transform, rotation, bone->local_transform);
            glm_scale(bone->local_transform, scale);
        }
    }
    
    /* go over every bone object */
    for (size_t obj_idx = 0; obj_idx < pScene3D->object_count; obj_idx++) {
        size_t bone_id = FindBoneByName(pScene3D, pScene3D->objects[obj_idx].name);
        if (bone_id == (size_t)-1) {
            continue;
        }

        /* mesh->bone * bone->bone */
        glm_mul(pScene3D->bones[bone_id].local_transform, pScene3D->bones[bone_id].offset_matrix, pScene3D->objects[obj_idx]._transformation);
        /* mesh->bone * bone->mesh, result is a model matrix that does animation. */
        glm_mul(pScene3D->bones[bone_id].offset_matrix_inv, pScene3D->objects[obj_idx]._transformation, pScene3D->objects[obj_idx]._transformation);

        /* if we have a parent, and the parent is also a bone, do this: */
        if (pScene3D->objects[obj_idx].parent && FindBoneByName(pScene3D, pScene3D->objects[obj_idx].parent->name) != (size_t)-1) {
            /* mesh * mesh */
            glm_mul(pScene3D->objects[obj_idx].parent->_transformation, pScene3D->objects[obj_idx]._transformation, pScene3D->objects[obj_idx]._transformation);
        }

        glm_mat4_copy(pScene3D->objects[obj_idx]._transformation, matrices.bone_matrices[bone_id]);
        glm_mul(pScene3D->bones[bone_id].offset_matrix, matrices.bone_matrices[bone_id], matrices.bone_matrices[bone_id]);
    }
}

bool LERenderScene3D(struct Scene3D *pScene3D) {
    StepAnimation(pScene3D);

    glm_perspective(1.0472f, (float)LESwapchainWidth/(float)LESwapchainHeight, 0.1f, 1000.f, matrices.projection);
    glm_look(render_info.cam_pos, render_info.dir_vec, (vec3){0, 1, 0}, matrices.view);

    struct Object *obj;
    for (size_t i = 0; i < pScene3D->object_count; i++) {
        obj = &pScene3D->objects[i];

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
                glm_translate(matrices.model, head->position);
                glm_quat_rotate(matrices.model, head->rotation, matrices.model);
                glm_scale(matrices.model, head->scale);

                head = head->parent;
            }

            SDL_PushGPUVertexUniformData(LECommandBuffer, 0, &matrices, sizeof(matrices));

            SDL_PushGPUFragmentUniformData(LECommandBuffer, 0, &mesh->material, sizeof(mesh->material));
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 1, &light_ubo, sizeof(light_ubo));
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 2, &render_info.cam_pos, sizeof(vec3));

            if (mesh->shader == TEXTURED_TEST_SHADER) {
                SDL_GPUTextureSamplerBinding sampler_binding;
                sampler_binding.texture = mesh->texture.gpu_texture;
                sampler_binding.sampler = mesh->texture.gpu_sampler;

                SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
            }

            SDL_DrawGPUIndexedPrimitives(render_pass, mesh->index_buffer.count, 1, 0, 0, 0);
        }
    }

    return true;
}

void LEDestroyScene3D(struct Scene3D *pScene3D) {
    for (; pScene3D->bone_count > 0; pScene3D->bone_count--) {
        struct Bone *bone = &pScene3D->bones[pScene3D->bone_count - 1];

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

    for (; pScene3D->object_count > 0; pScene3D->object_count--) {
        struct Object *object = &pScene3D->objects[pScene3D->object_count - 1];

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
    }
    if (pScene3D->objects) {
        SDL_free(pScene3D->objects);
    }

    /* loop through the lights and remove any lights imported from this scene */
    int i;
    for (i = 0; i < light_ubo.lights_count;) {
        if (light_ubo.lights[i].scene_ptr != (Uint64)pScene3D) {
            i++;
            continue;
        }
        SDL_memmove(&light_ubo.lights[i], &light_ubo.lights[i + 1], (light_ubo.lights_count - (i + 1)) * sizeof(struct Light));
        light_ubo.lights_count--;
    }

    SDL_free(pScene3D);
}

void LECleanupScene(void) {
    ClearButtonRegistry();

    switch (scene_loaded) {
        case SCENE_MAINMENU:
            MainMenuCleanup();
            break;
        case SCENE_OPTIONS:
            OptionsCleanup();
            break;
        case SCENE3D_INTRO:
            IntroCleanup();
            break;
        default:;
    }
}

void LEGrabMouse(void) {
    if (!SDL_SetWindowRelativeMouseMode(window, true)) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Failed to grab mouse! SDL Error: '%s'.\n", SDL_GetError());
    }
}

void LEReleaseMouse(void) {
    if (!SDL_SetWindowRelativeMouseMode(window, false)) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Failed to release mouse! SDL Error: '%s'.\n", SDL_GetError());
    }
}

static Uint64 last_frame_time;
static Uint64 now;

double LEFrametime = 0.0;
static double time_since_network_tick = 0.0;

float LEMouseRelX, LEMouseRelY;

SDL_GPUTexture *LESwapchainTexture, *LEDepthStencilTexture = NULL;
Uint32 LESwapchainWidth, LESwapchainHeight = 0;

bool LEStepRender(void) {
    now = SDL_GetTicksNS();

    static SDL_Event event;

    LEMouseRelX = 0;
    LEMouseRelY = 0;

    /* Is there a resize event here? if so, we'll apply the update only on the latest resize event */
    static bool window_resized = false;
    while (SDL_PollEvent(&event)) {
        /* If escape is held down OR a window close is requested, return false. */
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            LEScreenWidth = event.window.data1;
            LEScreenHeight = event.window.data2;

            window_resized = true;
        }

        /* If we're in the middle of a transition, don't handle any event. (except the 2 aforementioned) */
        if (scene_transition.active) {
            continue;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            switch (event.key.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    return false;
                case SDL_SCANCODE_TAB:
                    Navigate(event.key.mod & SDL_KMOD_SHIFT);
                    break;
                case SDL_SCANCODE_SPACE:
                    PressActiveButton();
                    break;
                default:
                    switch (scene_loaded) {
                        case SCENE3D_INTRO:
                            IntroKeyDown(event.key.scancode);
                        default:
                            ;
                    }
            }
        } else if (event.type == SDL_EVENT_KEY_UP) {
            switch (scene_loaded) {
                case SCENE3D_INTRO:
                    IntroKeyUp(event.key.scancode);
                default:
                    ;
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            ResetNavigation();
            LEMouseRelX += event.motion.xrel * options.cam_sens;
            LEMouseRelY += event.motion.yrel * options.cam_sens;
        }
    }

    if (window_resized && !InitGPURenderTexture()) {
        return false;
    }

        SDL_SetRenderDrawColorFloat(renderer, 0.f, 0.f, 0.f, SDL_ALPHA_OPAQUE_FLOAT);
        SDL_RenderClear(renderer);

        /* call the right render function for whatever scene we're running right now */
        switch (scene_loaded) {
            case SCENE_MAINMENU:
                if (!MainMenuRender()) {
                    return false;
                }
                break;
            case SCENE_OPTIONS:
                if (!OptionsRender()) {
                    return false;
                }
                break;
            case SCENE3D_INTRO:
                if (!IntroRender()) {
                    return false;
                }
                break;
            default:;
        }
    if (scene_transition.active) {
        scene_transition.perc += LEFrametime * 0.5f;
        if (scene_transition.dest != scene_loaded && scene_transition.perc >= 0.5f) {
            LECleanupScene();
            scene_loaded = scene_transition.dest;
            if (!InitCurrentScene()) {
                return false;
            }
        }
        if (scene_transition.perc >= 1.f) {
            scene_transition.perc = 1.f;
            scene_transition.active = false;
        }

        if (scene_transition.perc <= 0.5f) {
            SDL_SetRenderDrawColorFloat(renderer, 0.f, 0.f, 0.f, scene_transition.perc*2);
        } else {
            /* flip scene_transition.perc from (0.5->1.0) to (1.0->0.0) */
            SDL_SetRenderDrawColorFloat(renderer, 0.f, 0.f, 0.f, -((scene_transition.perc-1.f)*2));
        }
        SDL_RenderFillRect(renderer, NULL);
    }

    SDL_RenderPresent(renderer);

    LEFrametime = (now - last_frame_time) / 1000000000.0;
    time_since_network_tick += LEFrametime;

    last_frame_time = now;

    return true;
}
