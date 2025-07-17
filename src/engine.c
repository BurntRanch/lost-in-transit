#include "scenes/game/intro.h"
#include <SDL3/SDL_gpu.h>
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

void LEDestroyWindow(void) {
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;

        /* we have to set the other values to NULL because they're implicitly destroyed */
        renderer = NULL;
    }
}

/* Are we using SDL_gpu now? Active on Scene3Ds */
static bool is_using_gpu = false;

bool LEInitWindow(void) {
    if (window) {
        LEDestroyWindow();
    }

    if (!(window = SDL_CreateWindow(TITLE, LEScreenWidth, LEScreenHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE))) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 400, 300);

    if (!is_using_gpu) {
        /* We have to call this stupid function, oh well */
        if (!SDL_GetWindowSurface(window)) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Something went wrong while getting a surface! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
        
        if (!(renderer = SDL_GetRenderer(window))) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Something went wrong while getting the renderer! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    } else {
        if (!gpu_device && !(gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL))) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU Device! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to claim window for GPU device! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    }

    if (!SDL_GetWindowSize(window, &LEScreenWidth, &LEScreenHeight)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Something went wrong while getting renderer output size! (SDL Error: %s)\n", SDL_GetError());
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

void DestroyText(struct LE_Label * const pLEText) {
    if (pLEText->texture) {
        SDL_DestroyTexture(pLEText->texture);
        pLEText->texture = NULL;
    }

    if (pLEText->surface) {
        SDL_DestroySurface(pLEText->surface);
        pLEText->surface = NULL;
    }
}

bool UpdateText(struct LE_Label * const pLEText) {
    DestroyText(pLEText);

    if (!(pLEText->surface = TTF_RenderText_Blended_Wrapped(pLEGameFont, pLEText->text, 0, (SDL_Color){255,255,255,SDL_ALPHA_OPAQUE}, 0))) {
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

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(LECommandBuffer, window, &LESwapchainTexture, &LESwapchainWidth, &LESwapchainHeight)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire swapchain texture from command buffer! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

static inline bool FinishGPURendering() {
    if (!SDL_SubmitGPUCommandBuffer(LECommandBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to submit command buffer to GPU device! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

/* Starts using the GPU Device. reinitializes the window. */
static inline bool StartGPUDevice() {
    if (is_using_gpu) {
        return true;
    }

    is_using_gpu = true;

    LEInitWindow();

    /* We have to submit a command buffer (even if it's empty) to make the window visible. */
    /* We do this so that the user doesn't get confused when the window just disappears and doesn't appear for a few seconds. */
    if (!StartGPURendering() || !FinishGPURendering()) {
        return false;
    }

    return true;
}

/* Stops using the GPU Device. reinitializes the window. */
static inline bool StopGPUDevice() {
    if (!is_using_gpu) {
        return true;
    }

    is_using_gpu = false;

    LEInitWindow();

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
        if (!StopGPUDevice() || !MainMenuInit(renderer)) {
            return false;
        }
        break;
    case SCENE_OPTIONS:
        if (!StopGPUDevice() || !OptionsInit(renderer)) {
            return false;
        }
        break;
    case SCENE_PLAY:
        if (!StopGPUDevice() || !PlayInit(renderer)) {
            return false;
        }
        break;
    case SCENE_LOBBY:
        if (!StopGPUDevice() || !LobbyInit(renderer)) {
            return false;
        }
        break;
    case SCENE3D_INTRO:
        if (!StartGPUDevice() || !IntroInit(gpu_device)) {
            return false;
        }
        break;
    default:
        ;
    }

    scene_loaded = scene;
    return true;
}

static Uint8 scene_to_load = SCENE_UNKNOWN;

void LEScheduleLoadScene(const Uint8 scene) {
    scene_to_load = scene;
}

void LECleanupScene(void) {
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
    default:
        ;
    }
}

static Uint64 last_frame_time;
static Uint64 now;

double LEFrametime = 0.0;

SDL_GPUCommandBuffer *LECommandBuffer = NULL;
SDL_GPUTexture *LESwapchainTexture = NULL;
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
    while (SDL_PollEvent(&event)) {
        /* If escape is held down OR a window close is requested, return false. */
        if ((event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) || event.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            SDL_GetWindowSize(window, &LEScreenWidth, &LEScreenHeight);
        }
    }

    if (!SRPollConnections()) {
        return false;
    }

    if (!is_using_gpu) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer);
    } else {
        if (!StartGPURendering()) {
            return false;
        }
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
    default:
        ;
    }

    if (!is_using_gpu) {
        SDL_RenderPresent(renderer);
    } else {
        if (!FinishGPURendering()) {
            return false;
        }
    }
    
    LEFrametime = (now - last_frame_time) / 1000000000.0;

    last_frame_time = now;

    return true;
}
