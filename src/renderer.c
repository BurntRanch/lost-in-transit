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

#include <bits/time.h>
#include <stdio.h>
#include <time.h>

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
    window = SDL_CreateWindow(TITLE, 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "Something went wrong while creating a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
        fprintf(stderr, "Something went wrong while getting a surface! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    
    renderer = SDL_GetRenderer(window);
    if (!renderer) {
        fprintf(stderr, "Something went wrong while getting a window! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

static struct timespec last_frame_time;
static struct timespec now;

bool LEStepRender(double *frametime) {
    clock_gettime(CLOCK_MONOTONIC, &now);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                LEDestroyWindow();
                SDL_Quit();

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
        LEDestroyWindow();

        return false;
    }

    SDL_RenderPresent(renderer);
    
    if (frametime) {
        /* Frametime in milliseconds! */
        *frametime = difftime(now.tv_nsec, last_frame_time.tv_nsec) / 1000000;
    }

    last_frame_time = now;

    return true;
}
