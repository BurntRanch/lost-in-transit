#include "button.h"
#include "networking.h"
#include "options.h"
#include "scenes/game/intro.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#define TITLE "Lost In Transit"

#include "engine.h"
#include "scenes.h"
#include "label.h"

#include "scenes/main_menu.h"
#include "scenes/options.h"
#include "scenes/lobby.h"
#include "scenes/play.h"

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

#include "steam.hh"

#include <stdio.h>
#include <time.h>

TTF_Font *pLEGameFont = NULL;

/* Resolution defaults. */
int LEScreenWidth = 800;
int LEScreenHeight = 600;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_GPUDevice *gpu_device = NULL;

static SDL_Texture *render_texture = NULL;

/* we keep this open because why should we keep opening/closing it each frame? */
static SDL_GPUTransferBuffer *render_transferbuffer = NULL;

static bool InitGPURenderTexture() {
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

bool LEInitWindow(void) {
    LEDestroyWindow();

    LEDestroyGPU();

    if (LECommandBuffer) {
        LECommandBuffer = NULL;
    }

    if (options.vsync) {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    } else {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    }

    if (!(window = SDL_CreateWindow(TITLE, LEScreenWidth, LEScreenHeight, SDL_WINDOW_VULKAN))) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 400, 300);

    /* We have to call this stupid function, oh well */
    if (!(renderer = SDL_CreateRenderer(window, "vulkan"))) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Something went wrong while getting the renderer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!gpu_device && !(gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU Device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!InitGPURenderTexture()) {
        return false;
    }

    return true;
}

bool LEInitTTF(void) {
    if (!TTF_Init()) {
        fprintf(stderr, "Something went wrong while initializing SDL3_TTF! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

bool LEInitSteam(void) {
    return SRInitGNS();
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
static Uint8 scene_loaded = SCENE_NONE;

/* Initialize [LECommandBuffer] and [LESwapchain*] */
static inline bool StartGPURendering() {
    if (!(LECommandBuffer = SDL_AcquireGPUCommandBuffer(gpu_device))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire command buffer for GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

/* Downloads render result to render_texture */
static inline bool FinishGPURendering() {
    if (!LECommandBuffer) {
        return false;
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

    return true;
}

bool LELoadScene(const Uint8 scene) {
    if (!TTF_WasInit()) {
        fprintf(stderr, "Can't load scene when TTF is not initialized!");
        return false;
    }

    LECleanupScene();

    switch (scene) {
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
        case SCENE_PLAY:
            if (!PlayInit(renderer)) {
                return false;
            }
            break;
        case SCENE_LOBBY:
            if (!LobbyInit(renderer)) {
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

    scene_loaded = scene;
    return true;
}

static Uint8 scene_to_load = SCENE_UNKNOWN;

void LEScheduleLoadScene(const Uint8 scene) {
    scene_to_load = scene;
}

void LECleanupScene(void) {
    ClearButtonRegistry();

    if (!StartGPURendering()) {
        return;
    }

    /* clear the 3D scene */
    static SDL_GPUColorTargetInfo color_target_info;
    color_target_info.clear_color = (SDL_FColor){0.f, 0.f, 0.f, 1.f};
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = LESwapchainTexture;

    static SDL_GPUDepthStencilTargetInfo depth_stencil_target_info;
    depth_stencil_target_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_stencil_target_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_stencil_target_info.clear_depth = 1.0f;
    depth_stencil_target_info.cycle = false;
    depth_stencil_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target_info.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target_info.texture = LEDepthStencilTexture;

    static SDL_GPURenderPass *render_pass;
    if (!(render_pass = SDL_BeginGPURenderPass(LECommandBuffer, &color_target_info, 1, &depth_stencil_target_info))) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to begin render pass! (SDL Error: %s)\n", SDL_GetError());
        return;
    }
    SDL_EndGPURenderPass(render_pass);

    if (!FinishGPURendering()) {
        return;
    }

    switch (scene_loaded) {
        case SCENE_MAINMENU:
            MainMenuCleanup();
            break;
        case SCENE_OPTIONS:
            OptionsCleanup();
            break;
        case SCENE_PLAY:
            PlayCleanup();
            break;
        case SCENE_LOBBY:
            LobbyCleanup();
            break;
        case SCENE3D_INTRO:
            IntroCleanup();
            break;
        default:;
    }
}

static Uint64 last_frame_time;
static Uint64 now;

double LEFrametime = 0.0;
static double time_since_network_tick = 0.0;

SDL_GPUCommandBuffer *LECommandBuffer = NULL;
SDL_GPUTexture *LESwapchainTexture, *LEDepthStencilTexture = NULL;
Uint32 LESwapchainWidth, LESwapchainHeight = 0;

bool LEStepRender(void) {
    now = SDL_GetTicksNS();

    /* Load scheduled scenes (if any) */
    if (scene_to_load != SCENE_UNKNOWN) {
        if (!LELoadScene(scene_to_load)) {
            return false;
        }

        scene_to_load = SCENE_UNKNOWN;
    }

    static SDL_Event event;

    /* Is there a resize event here? if so, we'll apply the update only on the latest resize event */
    static bool window_resized = false;
    while (SDL_PollEvent(&event)) {
        /* If escape is held down OR a window close is requested, return false. */
        if ((event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) || event.type == SDL_EVENT_QUIT) {
            return false;
        } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            LEScreenWidth = event.window.data1;
            LEScreenHeight = event.window.data2;

            window_resized = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            /* start moving in the direction we want */
            enum MovementDirection direction = NETGetDirection();
            switch (event.key.scancode) {
                case SDL_SCANCODE_W:
                    direction |= MOVEMENT_FORWARD;
                    break;
                case SDL_SCANCODE_A:
                    direction |= MOVEMENT_LEFT;
                    break;
                case SDL_SCANCODE_S:
                    direction |= MOVEMENT_BACKWARD;
                    break;
                case SDL_SCANCODE_D:
                    direction |= MOVEMENT_RIGHT;
                    break;
                case SDL_SCANCODE_TAB:
                    Navigate(event.key.mod & SDL_KMOD_SHIFT);
                    break;
                case SDL_SCANCODE_SPACE:
                case SDL_SCANCODE_RETURN:
                    PressActiveButton();
                    break;
                default:;
            }

            NETChangeMovement(direction);
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            ResetNavigation();
        } else if (event.type == SDL_EVENT_KEY_UP) {
            /* stop moving in the direction */
            enum MovementDirection direction = NETGetDirection();
            switch (event.key.scancode) {
                case SDL_SCANCODE_W:
                    direction &= ~MOVEMENT_FORWARD;
                    break;
                case SDL_SCANCODE_A:
                    direction &= ~MOVEMENT_LEFT;
                    break;
                case SDL_SCANCODE_S:
                    direction &= ~MOVEMENT_BACKWARD;
                    break;
                case SDL_SCANCODE_D:
                    direction &= ~MOVEMENT_RIGHT;
                    break;
                default:;
            }

            NETChangeMovement(direction);
        }
    }

    if (window_resized && !InitGPURenderTexture()) {
        return false;
    }

    while (time_since_network_tick >= 1.0/NETWORKING_TICKRATE) {
        if (!SRPollConnections()) {
            return false;
        }
    
        NETTickServer();
        NETTickClient();

        time_since_network_tick -= 1.0/NETWORKING_TICKRATE;
    }

    if (!StartGPURendering()) {
        return false;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    if (!SDL_RenderTexture(renderer, render_texture, NULL, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to render GPU target! (SDL Error: %s)\n", SDL_GetError());
    }

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
        case SCENE_PLAY:
            if (!PlayRender()) {
                return false;
            }
            break;
        case SCENE_LOBBY:
            if (!LobbyRender()) {
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

    if (!FinishGPURendering()) {
        return false;
    }
    SDL_RenderPresent(renderer);

    LEFrametime = (now - last_frame_time) / 1000000000.0;
    time_since_network_tick += LEFrametime;

    last_frame_time = now;

    return true;
}
