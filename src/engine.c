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

#define IN_FLIGHT_FRAMES 2 

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

static SDL_GPURenderPass *render_pass = NULL;
static struct RenderInfo render_info;

static struct FlightFrame {
    SDL_GPUTexture *render_target;
    SDL_GPUTexture *depth_stencil_target;

    SDL_GPUTransferBuffer *render_transferbuffer;

    SDL_GPUFence *fence;
} swapchain_textures[IN_FLIGHT_FRAMES];

static size_t active_frame = 0;

static struct SceneTransition {
    enum Scene dest;
    /* how far we're in the transition */
    float perc;

    bool active;
} scene_transition;

/* works even if gpu_device is NULL */
void FreeGPUResources() {
    if (gpu_device && render_texture) {
        SDL_DestroyTexture(render_texture);
    }
    render_texture = NULL;

    for (size_t i = 0; i < IN_FLIGHT_FRAMES; i++) {
        if (gpu_device) {
            if (swapchain_textures[i].render_target) {
                SDL_ReleaseGPUTexture(gpu_device, swapchain_textures[i].render_target);
            }
            if (swapchain_textures[i].depth_stencil_target) {
                SDL_ReleaseGPUTexture(gpu_device, swapchain_textures[i].depth_stencil_target);
            }
            if (swapchain_textures[i].fence) {
                SDL_ReleaseGPUFence(gpu_device, swapchain_textures[i].fence);
            }
            if (swapchain_textures[i].render_transferbuffer) {
                SDL_ReleaseGPUTransferBuffer(gpu_device, swapchain_textures[i].render_transferbuffer);
            }
        }

        swapchain_textures[i].render_target = NULL;
        swapchain_textures[i].depth_stencil_target = NULL;
        swapchain_textures[i].fence = NULL;
        swapchain_textures[i].render_transferbuffer = NULL;
    }
}

static bool InitGPURenderTexture(void) {
    static SDL_GPUTextureCreateInfo gpu_texture_create_info;
    gpu_texture_create_info.type = SDL_GPU_TEXTURETYPE_2D;
    gpu_texture_create_info.props = 0;
    gpu_texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    gpu_texture_create_info.width = LEScreenWidth;
    gpu_texture_create_info.height = LEScreenHeight;
    gpu_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gpu_texture_create_info.num_levels = 1;
    gpu_texture_create_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpu_texture_create_info.layer_count_or_depth = 1;

    static SDL_GPUTextureCreateInfo depth_stencil_texture_create_info;
    depth_stencil_texture_create_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_stencil_texture_create_info.props = 0;
    depth_stencil_texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_stencil_texture_create_info.width = LEScreenWidth;
    depth_stencil_texture_create_info.height = LEScreenHeight;
    depth_stencil_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    depth_stencil_texture_create_info.num_levels = 1;
    depth_stencil_texture_create_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    depth_stencil_texture_create_info.layer_count_or_depth = 1;
    
    static SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info;
    transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_buffer_create_info.props = 0;
    transfer_buffer_create_info.size = LEScreenWidth * LEScreenHeight * 4;  /* 4 bytes for each pixel */

    for (size_t i = 0; i < IN_FLIGHT_FRAMES; i++) {
        if (!(swapchain_textures[i].render_target = SDL_CreateGPUTexture(gpu_device, &gpu_texture_create_info))) {
            fprintf(stderr, "Failed to create GPU texture! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
        if (!(swapchain_textures[i].depth_stencil_target = SDL_CreateGPUTexture(gpu_device, &depth_stencil_texture_create_info))) {
            fprintf(stderr, "Failed to create GPU texture! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
        if (!(swapchain_textures[i].render_transferbuffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_create_info))) {
            fprintf(stderr, "Failed to create GPU Transfer buffer! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    }

    /* the colors are reversed so this is effectively RGBA. Please don't ask me anything about this. */
    if (!(render_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, LEScreenWidth, LEScreenHeight))) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to create render_texture! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void LEDestroyGPU(void) {
    FreeGPUResources();

    if (gpu_device) {
        SDL_DestroyGPUDevice(gpu_device);
    }
    gpu_device = NULL;
}

void LEDestroyWindow(void) {
    if (window) {
        SDL_DestroyWindow(window);
    }

    window = NULL;

    /* we have to set the other values to NULL because they're implicitly destroyed */
    renderer = NULL;
    render_texture = NULL;
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

bool LEInitPipeline(struct GraphicsPipeline *pPipelineOut, enum PipelineSelection selection) {
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

    switch (selection << 16) {
        case PIPELINE_VERTEX_DEFAULT << 16:
            if (!LoadShader("shaders/vertex/vertex.glsl.spv", (Uint8 **)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size)) {
                return false;
            }
            break;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No or unknown vertex shader selected! (got %d)\n", selection << 16);
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

    switch (selection >> 16) {
        case PIPELINE_FRAG_TEXTURED_CEL >> 16:
            fragment_shader_create_info.num_samplers = 1;
            if (!LoadShader("shaders/textured/test_shader.glsl.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
                return false;
            }
            break;
        case PIPELINE_FRAG_UNTEXTURED_CEL >> 16:
            if (!LoadShader("shaders/untextured/test_shader.glsl.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size)) {
                return false;
            }
            break;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No or unknown fragment shader selected! (got %d)\n", selection >> 16);
            return false;
    }

    if (!(pPipelineOut->vertex_shader = SDL_CreateGPUShader(gpu_device, &vertex_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test vertex GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(pPipelineOut->fragment_shader = SDL_CreateGPUShader(gpu_device, &fragment_shader_create_info))) {
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
    graphics_pipeline_create_info.vertex_shader = pPipelineOut->vertex_shader;
    graphics_pipeline_create_info.fragment_shader = pPipelineOut->fragment_shader;
    graphics_pipeline_create_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    graphics_pipeline_create_info.props = 0;

    if (!(pPipelineOut->graphics_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &graphics_pipeline_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test graphics pipeline! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

bool LEPrepareGPURendering(void) {
    if (!(LECommandBuffer = SDL_AcquireGPUCommandBuffer(gpu_device))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire command buffer for GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    render_pass = NULL;

    return true;
}

SDL_GPUDevice *LEGetGPUDevice() {
    return gpu_device;
}

bool CopyFrameToRenderTexture(size_t frame) {
    static void *pixels;
    if (!(pixels = SDL_MapGPUTransferBuffer(gpu_device, swapchain_textures[frame].render_transferbuffer, false))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to map GPU Transfer buffer to memory! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    static void *dst_pixels;
    static int pitch;
    
    if (!SDL_LockTexture(render_texture, NULL, &dst_pixels, &pitch)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to lock render_texture! If this happens too often, please report this issue! (SDL Error: %s)\n", SDL_GetError()); 
        return true;
    }

    SDL_memcpy(dst_pixels, pixels, pitch * LEScreenHeight);

    SDL_UnmapGPUTransferBuffer(gpu_device, swapchain_textures[frame].render_transferbuffer);
    SDL_UnlockTexture(render_texture);

    return true;
}

bool LEStartGPURender(void) {
    active_frame = (active_frame + 1) % (IN_FLIGHT_FRAMES);
    
    if (swapchain_textures[active_frame].fence) {
        SDL_WaitForGPUFences(gpu_device, true, &swapchain_textures[active_frame].fence, 1);
        SDL_ReleaseGPUFence(gpu_device, swapchain_textures[active_frame].fence);
        swapchain_textures[active_frame].fence = NULL;
    }

    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){0.f, 0.f, 0.f, 1.f};
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = swapchain_textures[active_frame].render_target;

    static SDL_GPUDepthStencilTargetInfo depth_stencil_target_info;
    depth_stencil_target_info.clear_stencil = 255;
    depth_stencil_target_info.clear_depth = 1.0f;
    depth_stencil_target_info.cycle = false;
    depth_stencil_target_info.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_stencil_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target_info.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target_info.texture = swapchain_textures[active_frame].depth_stencil_target;

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
    src.w = LEScreenWidth;
    src.h = LEScreenHeight;
    src.z = 0;
    src.d = 1;
    src.layer = 0;
    src.texture = swapchain_textures[active_frame].render_target;
    src.mip_level = 0;

    static SDL_GPUTextureTransferInfo texture_transfer_info;
    texture_transfer_info.offset = 0;
    texture_transfer_info.pixels_per_row = LEScreenWidth;
    texture_transfer_info.rows_per_layer = LEScreenHeight;
    texture_transfer_info.transfer_buffer = swapchain_textures[active_frame].render_transferbuffer;

    SDL_DownloadFromGPUTexture(copy_pass, &src, &texture_transfer_info);

    SDL_EndGPUCopyPass(copy_pass);

    if (!(swapchain_textures[active_frame].fence = SDL_SubmitGPUCommandBufferAndAcquireFence(LECommandBuffer))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to submit command buffer to GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    bool found = false;
    size_t latest_match;
#if IN_FLIGHT_FRAMES > 1
    for (size_t i = (active_frame + 1) % (IN_FLIGHT_FRAMES); i != active_frame; i = (i + 1) % (IN_FLIGHT_FRAMES)) {
        /* if this frame was never rendered, skip. (only happens in the first few frames, then this should never happen.) */
        if (!swapchain_textures[i].fence) {
            continue;
        }
        /* if this swapchain texture hasn't finished rendering, break out. */
        if (!SDL_QueryGPUFence(gpu_device, swapchain_textures[i].fence)) {
            break;
        }

        found = true;
        latest_match = i;
        SDL_ReleaseGPUFence(gpu_device, swapchain_textures[i].fence);
        swapchain_textures[i].fence = NULL;
    }
#else
    found = true;
    latest_match = active_frame;
    SDL_WaitForGPUFences(gpu_device, true, &swapchain_textures[active_frame].fence, 1);
    SDL_ReleaseGPUFence(gpu_device, swapchain_textures[active_frame].fence);
    swapchain_textures[active_frame].fence = NULL;
#endif

    if (found) {
        CopyFrameToRenderTexture(latest_match);
    }

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

struct RenderInfo *LEGetRenderInfo(void) {
    return &render_info;
}

static inline void StepAnimation(struct Model *pModel) {
    if (pModel->animation_playing) {
        pModel->animation_time += LEFrametime * pModel->current_animation.ticks_per_sec;
        
        if (pModel->animation_time >= pModel->current_animation.duration) {
            pModel->animation_time = SDL_fmod(pModel->animation_time, pModel->current_animation.duration);
        }

        /* update all bone local transforms */
        for (size_t bone_idx = 0; bone_idx < pModel->bone_count; bone_idx++) {
            vec3 position = {0, 0, 0};
            vec3 scale = {1, 1, 1};
            vec4 rotation = {0, 0, 0, 1};

            struct Bone *bone = &pModel->bones[bone_idx];

            size_t key_idx = 0;
            for (key_idx = 0; key_idx < bone->position_key_count; key_idx++) {
                if (bone->position_keys[key_idx].timestamp < pModel->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(position, bone->position_keys->value, sizeof(position));
                    break;
                }

                double last_timestamp = bone->position_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->position_keys[key_idx].timestamp;

                glm_vec3_lerp(bone->position_keys[key_idx - 1].value, bone->position_keys[key_idx].value, (pModel->animation_time - last_timestamp) / (new_timestamp - last_timestamp), position);
                //aiVector3Lerp(&bone->position_keys[key_idx - 1].value, &bone->position_keys[key_idx].value, (scene->animation_time - last_timestamp) / (new_timestamp - last_timestamp), &position);
                break;
            }

            for (key_idx = 0; key_idx < bone->rotation_key_count; key_idx++) {
                if (bone->rotation_keys[key_idx].timestamp < pModel->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(rotation, bone->rotation_keys->value, sizeof(rotation));
                    break;
                }

                double last_timestamp = bone->rotation_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->rotation_keys[key_idx].timestamp;

                glm_quat_slerp(bone->rotation_keys[key_idx - 1].value, bone->rotation_keys[key_idx].value, (pModel->animation_time - last_timestamp) / (new_timestamp - last_timestamp), rotation);
                break;
            }

            for (key_idx = 0; key_idx < bone->scale_key_count; key_idx++) {
                if (bone->scale_keys[key_idx].timestamp < pModel->animation_time) {
                    continue;
                }

                if (key_idx == 0) {
                    SDL_memcpy(scale, bone->scale_keys->value, sizeof(scale));
                    break;
                }

                double last_timestamp = bone->scale_keys[key_idx - 1].timestamp;
                double new_timestamp = bone->scale_keys[key_idx].timestamp;

                glm_vec3_lerp(bone->scale_keys[key_idx - 1].value, bone->scale_keys[key_idx].value, (pModel->animation_time - last_timestamp) / (new_timestamp - last_timestamp), scale);
                break;
            }
            
            glm_mat4_identity(bone->local_transform);
            glm_translate(bone->local_transform, position);
            glm_quat_rotate(bone->local_transform, rotation, bone->local_transform);
            glm_scale(bone->local_transform, scale);
        }
    }
    
    /* go over every bone object */
    for (size_t obj_idx = 0; obj_idx < pModel->object_count; obj_idx++) {
        size_t bone_id = MLFindBoneByName(pModel, pModel->objects[obj_idx].name);
        if (bone_id == (size_t)-1) {
            continue;
        }

        /* mesh->bone * bone->bone */
        glm_mul(pModel->bones[bone_id].local_transform, pModel->bones[bone_id].offset_matrix, pModel->objects[obj_idx]._transformation);
        /* mesh->bone * bone->mesh, result is a model matrix that does animation. */
        glm_mul(pModel->bones[bone_id].offset_matrix_inv, pModel->objects[obj_idx]._transformation, pModel->objects[obj_idx]._transformation);

        /* if we have a parent, and the parent is also a bone, do this: */
        if (pModel->objects[obj_idx].parent && MLFindBoneByName(pModel, pModel->objects[obj_idx].parent->name) != (size_t)-1) {
            /* mesh * mesh */
            glm_mul(pModel->objects[obj_idx].parent->_transformation, pModel->objects[obj_idx]._transformation, pModel->objects[obj_idx]._transformation);
        }

        glm_mat4_copy(pModel->objects[obj_idx]._transformation, matrices.bone_matrices[bone_id]);
        glm_mul(pModel->bones[bone_id].offset_matrix, matrices.bone_matrices[bone_id], matrices.bone_matrices[bone_id]);
    }
}

bool LERenderModel(struct Model *pScene3D) {
    StepAnimation(pScene3D);

    glm_perspective(1.0472f, (float)LEScreenWidth/(float)LEScreenHeight, 0.1f, 1000.f, matrices.projection);
    glm_look(render_info.cam_pos, render_info.dir_vec, (vec3){0, 1, 0}, matrices.view);

    struct Object *obj;
    for (size_t i = 0; i < pScene3D->object_count; i++) {
        obj = &pScene3D->objects[i];

        for (size_t mesh_idx = 0; mesh_idx < obj->mesh_count; mesh_idx++) {
            struct Mesh *mesh = &obj->meshes[mesh_idx];

            SDL_BindGPUGraphicsPipeline(render_pass, mesh->pipeline->graphics_pipeline);

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
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 1, &MLLightUBO, sizeof(MLLightUBO));
            SDL_PushGPUFragmentUniformData(LECommandBuffer, 2, &render_info.cam_pos, sizeof(vec3));

            if (mesh->texture.gpu_sampler && mesh->texture.gpu_texture) {
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

            /* arbitrary number, freely changable but make sure it's the same across both */
            LEMouseRelX += event.motion.xrel * options.cam_sens;
            LEMouseRelY += event.motion.yrel * options.cam_sens;
        }
    }

    if (window_resized) {
        FreeGPUResources();

        if (!InitGPURenderTexture()) {
            return false;
        }
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
        scene_transition.perc += LEFrametime;
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
