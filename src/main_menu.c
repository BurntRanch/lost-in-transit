#include "main_menu.h"
#include "engine.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>
#include <string.h>

static SDL_Renderer *renderer = NULL;

static struct LE_Text game_text;

bool MainMenuInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!pLEGameFont) {
        fprintf(stderr, "Trying to load text without loading the font!");
        return false;
    }

    game_text.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
    game_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    game_text.text = SDL_malloc(256);
    SDL_memcpy(game_text.text, "Lost in Transit", 16);
    if (!UpdateText(&game_text)) {
        return false;
    }

    return true;
}

static SDL_FRect dstrect;

bool MainMenuRender(const double * const delta) {
    dstrect.x = 10;
    dstrect.y = 5;
    dstrect.w = game_text.surface->w;
    dstrect.h = game_text.surface->h;
    if (!SDL_RenderTexture(renderer, game_text.texture, NULL, &dstrect)) {
        fprintf(stderr, "Failed to draw text for main menu! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    snprintf(game_text.text, 255, "Lost in Transit (delta: %f)", *delta);
    UpdateText(&game_text);

    return true;
}

void MainMenuCleanup(void) {
    DestroyText(&game_text);
}
