#include "scenes/options.h"
#include "engine.h"
#include "scenes.h"
#include "label.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <stdio.h>

static SDL_Renderer *renderer = NULL;

static struct LE_Label title_label;
static struct SDL_FRect title_dstrect = { 0, 0, 0, 0 };

static double time_elapsed;

bool OptionsInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    time_elapsed = 0.0;

    title_label.text = "nothing here! switching back in 5 seconds";
    title_label.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    title_label.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
    if (!UpdateText(&title_label)) {
        return false;
    }

    title_dstrect.w = title_label.surface->w;
    title_dstrect.h = title_label.surface->h;

    return true;
}

bool OptionsRender(void) {
    time_elapsed += LEFrametime;

    if (time_elapsed > 5) {
        LEScheduleLoadScene(SCENE_MAINMENU);
    }

    title_dstrect.x = LEScreenWidth * 0.0125;
    title_dstrect.y = LEScreenHeight * 0.0125;

    if (!SDL_RenderTexture(renderer, title_label.texture, NULL, &title_dstrect)) {
        fprintf(stderr, "Failed to render options text! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void OptionsCleanup(void) {
    DestroyText(&title_label);
}
