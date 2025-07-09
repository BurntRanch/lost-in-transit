#include "scenes/options.h"
#include "engine.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <stdio.h>

static SDL_Renderer *renderer = NULL;

static struct LE_Text title_text;
static struct SDL_FRect title_dstrect = { 0, 0, 0, 0 };

static double time_elapsed;

bool OptionsInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    time_elapsed = 0.0;

    title_text.text = "nothing here! switching back in 5 seconds";
    title_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    title_text.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
    if (!UpdateText(&title_text)) {
        return false;
    }

    title_dstrect.w = title_text.surface->w;
    title_dstrect.h = title_text.surface->h;

    return true;
}

bool OptionsRender(const double * const delta) {
    time_elapsed += *delta;

    if (time_elapsed > 5) {
        LEScheduleLoadScene(SCENE_MAINMENU);
    }

    title_dstrect.x = LEScreenWidth * 0.0125;
    title_dstrect.y = LEScreenHeight * 0.0125;

    if (!SDL_RenderTexture(renderer, title_text.texture, NULL, &title_dstrect)) {
        fprintf(stderr, "Failed to render options text! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void OptionsCleanup(void) {
    DestroyText(&title_text);
}
