#include "main_menu.h"
#include "scenes.h"
#define TITLE "Lost In Transit"

#include "renderer.h"

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

#include <bits/time.h>
#include <stdio.h>
#include <time.h>

TTF_Font *pLEGameFont = NULL;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_Surface *surface = NULL;

static TTF_TextEngine *text_engine = NULL;

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
    if (!(window = SDL_CreateWindow(TITLE, 800, 600, SDL_WINDOW_VULKAN))) {
        fprintf(stderr, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!(surface = SDL_GetWindowSurface(window))) {
        fprintf(stderr, "Something went wrong while getting a surface! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    
    if (!(renderer = SDL_GetRenderer(window))) {
        fprintf(stderr, "Something went wrong while getting a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

bool LEInitTTF(void) {
    if (!TTF_Init()) {
        fprintf(stderr, "Something went wrong while initializing SDL3_TTF! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!(text_engine = TTF_CreateRendererTextEngine(renderer))) {
        fprintf(stderr, "Couldn't create Renderer Text Engine! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

/* What's the currently loaded scene? Refer to scenes.h for values */
static Uint8 scene_loaded = SCENE_NONE;

bool LELoadScene(const Uint8 scene) {
    if (!text_engine) {
        fprintf(stderr, "Can't load scene when TTF is not initialized!");
        return false;
    }

    LECleanupScene();

    switch (scene) {
    case SCENE_MAINMENU:
        if (!MainMenuInit(text_engine)) {
            return false;
        }
    case SCENE_NONE:
    default:
        ;
    }

    scene_loaded = scene;
    return true;
}

void LECleanupScene() {
    switch (scene_loaded) {
    case SCENE_MAINMENU:
        MainMenuCleanup();
    case SCENE_NONE:
    default:
        ;
    }
}

static struct timespec last_frame_time;
static struct timespec now;

bool LEStepRender(double *frametime) {
    clock_gettime(CLOCK_MONOTONIC, &now);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                return false;
            }
        }
    }

    SDL_ClearSurface(surface, 0, 0, 0, 0);

    SDL_FRect rect;
    rect.x = 200;
    rect.y = 150;
    rect.w = 400;
    rect.h = 450;

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, SDL_ALPHA_OPAQUE);
    if (!SDL_RenderFillRect(renderer, &rect)) {
        fprintf(stderr, "Something went wrong while rendering a rect! (SDL Error Code: %s)\n", SDL_GetError());

        return false;
    }
    
    switch (scene_loaded) {
    case SCENE_MAINMENU:
        if (!MainMenuRender()) {
            return false;
        }
    case SCENE_NONE:
    default:
        ;
    }

    SDL_RenderPresent(renderer);
    
    if (frametime) {
        /* Frametime in milliseconds! */
        *frametime = difftime(now.tv_nsec, last_frame_time.tv_nsec) / 1000000;
    }

    last_frame_time = now;

    return true;
}
