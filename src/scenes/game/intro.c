#include "scenes/game/intro.h"
#include "cglm/types.h"
#include "engine.h"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <cglm/cglm.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>

struct Shader {
    SDL_GPUShader *vertex;
    SDL_GPUShader *fragment;
};

struct Vertex {
    vec2 vert;
};

static SDL_GPUDevice *gpu_device = NULL;

static struct Shader test_shader;
static SDL_GPUGraphicsPipeline *test_pipeline = NULL;
static SDL_GPUBuffer *test_vertex_buffer = NULL;

static const struct Vertex vertices[3] = {  { { -1.0f, 0.0f } },
                                            { { 0.0f, 1.0f } },
                                            { { 1.0f, 0.0f } }, };

/* Don't laugh */
static inline bool LoadShader(const char *fileName, Uint8 **ppBufferOut, size_t *pSizeOut) {
    FILE *file = fopen(fileName, "rb");

    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open shader file %s! (errno: %d)\n", fileName, errno);
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    *pSizeOut = ftell(file);
    *ppBufferOut = SDL_malloc(*pSizeOut);

    rewind(file);

    fread(*ppBufferOut, *pSizeOut, *pSizeOut, file);

    return true;
}

bool IntroInit(SDL_GPUDevice *pGPUDevice) {
    gpu_device = pGPUDevice;

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

    LoadShader("shaders/test_shader.vert.spv", (Uint8 **)&vertex_shader_create_info.code, &vertex_shader_create_info.code_size);

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

    LoadShader("shaders/test_shader.frag.spv", (Uint8 **)&fragment_shader_create_info.code, &fragment_shader_create_info.code_size);

    if (!(test_shader.vertex = SDL_CreateGPUShader(gpu_device, &vertex_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test vertex GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!(test_shader.fragment = SDL_CreateGPUShader(gpu_device, &fragment_shader_create_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create test fragment GPU shader! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

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
    graphics_pipeline_create_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
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

bool IntroRender(void) {
    /* initialize vertex buffer if not initialized */
    if (!test_vertex_buffer) {
        SDL_GPUCopyPass *copy_pass;

        if (!(copy_pass = SDL_BeginGPUCopyPass(LECommandBuffer))) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin GPU copy pass! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }

        SDL_GPUBufferCreateInfo vertex_buffer_create_info;
        vertex_buffer_create_info.props = 0;
        vertex_buffer_create_info.size = sizeof(vertices);
        vertex_buffer_create_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

        if (!(test_vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &vertex_buffer_create_info))) {
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

        SDL_memcpy(data, &vertices, sizeof(vertices));

        SDL_GPUTransferBufferLocation src;
        src.offset = 0;
        src.transfer_buffer = transfer_buffer;

        SDL_GPUBufferRegion dst;
        dst.offset = 0;
        dst.size = vertex_buffer_create_info.size;
        dst.buffer = test_vertex_buffer;

        SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

        SDL_EndGPUCopyPass(copy_pass);

        return true;
    }

    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){ 0.f, 0.f, 0.f, 1.f };
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = LESwapchainTexture;

    static SDL_GPURenderPass *render_pass;

    static SDL_GPUViewport viewport = {0, 0, 800, 600, 0.0f, 1.0f};

    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, NULL))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, test_pipeline);
    SDL_SetGPUViewport(render_pass, &viewport);

    SDL_GPUBufferBinding buffer_binding;
    buffer_binding.buffer = test_vertex_buffer;
    buffer_binding.offset = 0;
    
    SDL_BindGPUVertexBuffers(render_pass, 0, &buffer_binding, 1);

    SDL_DrawGPUPrimitives(render_pass, sizeof(vertices) / sizeof(struct Vertex), 1, 0, 0);

    SDL_EndGPURenderPass(render_pass);

    return true;
}

void IntroCleanup(void) {
    
}