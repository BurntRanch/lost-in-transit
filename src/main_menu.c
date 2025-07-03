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

#define FIXED_UPDATE_TIME 0.016

static SDL_Renderer *renderer = NULL;

static struct LE_Text game_text;
static float game_text_scale = 1.0f;

bool MainMenuInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!LEGameFont) {
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

static SDL_FRect dstrect = {0, 0, 0, 0};

/* time elapsed since first render function */
static double total_time = 0.0;
/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_counter = 0.0;

bool MainMenuRender(const double * const delta) {
    snprintf(game_text.text, 255, "Lost in Transit (delta: %f)", *delta);
    UpdateText(&game_text);

    total_time += *delta;
    fixed_update_counter += *delta;

    while (fixed_update_counter >= FIXED_UPDATE_TIME) {
        /* Fixed update area! */
        game_text_scale = (SDL_sinf(total_time) + 1) / 2;   // from zero to 1, smoooothllyyyyy!! ;-)

        fixed_update_counter -= FIXED_UPDATE_TIME;
    }

    /* Width and height are obvious.
     * dstrect.x will be at the middle of the screen, offsetted to the left by half the width (centering).
     * dstrect.y will be at the middle of the screen, offsetted to the top by half the height (centering).
     */
    
    dstrect.w = game_text.surface->w * game_text_scale;
    dstrect.h = game_text.surface->h * game_text_scale;

    dstrect.x = LEScreenWidth * 0.5 - dstrect.w * 0.5;
    dstrect.y = LEScreenHeight * 0.5 - dstrect.h * 0.5;

    if (!SDL_RenderTexture(renderer, game_text.texture, NULL, &dstrect)) {
        fprintf(stderr, "Failed to draw text for main menu! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void MainMenuCleanup(void) {
    DestroyText(&game_text);
}
