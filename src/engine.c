#define TITLE "Lost In Transit"

#include "engine.h"
#include "scenes.h"
#include "label.h"

#include "scenes/main_menu.h"
#include "scenes/options.h"
#include "scenes/host.h"
#include "scenes/play.h"
#include "scenes/connect.h"

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

int LEScreenWidth, LEScreenHeight;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_Surface *surface = NULL;

void LEDestroyWindow(void) {
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;

        /* we have to set the other values to NULL because they're implicitly destroyed */
        renderer = NULL;
        surface = NULL;
    }
}

bool LEInitWindow(void) {
    if (!(window = SDL_CreateWindow(TITLE, 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE))) {
        fprintf(stderr, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 400, 300);

    if (!(surface = SDL_GetWindowSurface(window))) {
        fprintf(stderr, "Something went wrong while getting a surface! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    
    if (!(renderer = SDL_GetRenderer(window))) {
        fprintf(stderr, "Something went wrong while getting a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_GetRenderOutputSize(renderer, &LEScreenWidth, &LEScreenHeight)) {
        fprintf(stderr, "Something went wrong while getting renderer output size! (SDL Error Code: %s)\n", SDL_GetError());
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

    if (!(pLEText->surface = TTF_RenderText_Shaded_Wrapped(pLEGameFont, pLEText->text, 0, pLEText->fg, pLEText->bg, 0))) {
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
    case SCENE_HOST:
        if (!HostInit(renderer)) {
            return false;
        }
        break;
    case SCENE_CONNECT:
        if (!ConnectInit(renderer)) {
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
    case SCENE_HOST:
        HostCleanup();
        break;
    case SCENE_CONNECT:
        ConnectCleanup();
        break;
    default:
        ;
    }
}

static Uint64 last_frame_time;
static Uint64 now;

double LEFrametime = 0.0;

bool LEStepRender(void) {
    now = SDL_GetTicksNS();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* If escape is held down OR a window close is requested, return false. */
        if ((event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            return false;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            SDL_GetRenderOutputSize(renderer, &LEScreenWidth, &LEScreenHeight);
        }
    }

    if (!SRPollConnections()) {
        return false;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    /* Load scheduled scenes (if any) */
    if (scene_to_load != SCENE_UNKNOWN) {
        if (!LELoadScene(scene_to_load)) {
            return false;
        }

        scene_to_load = SCENE_UNKNOWN;
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
    case SCENE_HOST:
        if (!HostRender()) {
            return false;
        }
        break;
    case SCENE_CONNECT:
        if (!ConnectRender()) {
            return false;
        }
        break;
    default:
        ;
    }

    SDL_RenderPresent(renderer);
    
    LEFrametime = (now - last_frame_time) / 1000000000.0;

    last_frame_time = now;

    return true;
}
