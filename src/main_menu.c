#include "main_menu.h"
#include "engine.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
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

static struct LE_Text play_text;
static float play_text_scale = 1.0f;
static float play_text_angle = 0.f;

static bool play_button_active = false;
static bool play_button_held = false;

/* Increases by 5% every 60th of a second. This variable basically defines how rotated the play text angle is, relative to -5 degrees. */
static float play_button_angle_percentage = 0.0f;

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

    play_text.fg = (SDL_Color){ 200, 140, 140, SDL_ALPHA_OPAQUE };
    play_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    play_text.text = "Play!";
    if (!UpdateText(&play_text)) {
        return false;
    }

    return true;
}

static SDL_FRect dstrect = {0, 0, 0, 0};

/* time elapsed since first render function (unused) */
// static double total_time = 0.0;
/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_counter = 0.0;

static void PlayButtonPressed() {
    LEScheduleLoadScene(SCENE_NONE);
}

/* https://en.wikipedia.org/wiki/Smoothstep */
float smoothstep(float edge0, float edge1, float x)
{
    // Scale, bias and saturate x to 0..1 range
    x = SDL_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*(3 - 2 * x);
}

bool MainMenuRender(const double * const delta) {
    // total_time += *delta;
    fixed_update_counter += *delta;

    /* tacky workaround so that we can check the bounds of mouse hovers on the play button :p */
    dstrect.x = LEScreenWidth * 0.0225;
    dstrect.y = LEScreenHeight * 0.25;
    dstrect.w = play_text.surface->w * play_text_scale;
    dstrect.h = play_text.surface->h * play_text_scale;

    while (fixed_update_counter >= FIXED_UPDATE_TIME) {
        /* Check if the user is hovering over play button */
        float x, y;

        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&x, &y);
        bool mouse1_held = mouse_state & SDL_BUTTON_LMASK;

        /* If hovering, check if the play button is inactive. If it's inactive, reload the text with an active background. 
         * Otherwise, if the play button is active, reload the text with an inactive background.
         */
        if ((dstrect.x <= x && x <= dstrect.w + dstrect.x) &&
            (dstrect.y <= y && y <= dstrect.h + dstrect.y)) {
                if (!play_button_active || (mouse1_held != play_button_held)) {
                    if (play_button_active && !mouse1_held) {
                        PlayButtonPressed();
                    }

                    play_button_active = true;
                    play_button_held = mouse1_held;

                    if (play_button_held) {
                        play_text.fg = (SDL_Color){ 200, 200, 200, SDL_ALPHA_OPAQUE };
                    } else {
                        play_text.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
                    }

                    if (!UpdateText(&play_text)) {
                        return false;
                    }
                }
            } else if (play_button_active) {
                play_button_active = false;

                play_text.fg = (SDL_Color){ 200, 140, 140, SDL_ALPHA_OPAQUE };
                if (!UpdateText(&play_text)) {
                    return false;
                }
            }
        
        if (play_button_active && play_button_angle_percentage <= 0.95f) {
            play_button_angle_percentage += 0.05f;
        } else if (!play_button_active && play_button_angle_percentage >= 0.05f) {
            play_button_angle_percentage -= 0.05f;
        }
        play_text_angle = -smoothstep(0.f, 1.f, play_button_angle_percentage)*5;

        fixed_update_counter -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, play_text.texture, NULL, &dstrect, play_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Play button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    
    dstrect.x = LEScreenWidth * 0.0125;
    dstrect.y = LEScreenHeight * 0.0125;
    dstrect.w = game_text.surface->w;
    dstrect.h = game_text.surface->h;

    if (!SDL_RenderTexture(renderer, game_text.texture, NULL, &dstrect)) {
        fprintf(stderr, "Failed to draw the game title! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void MainMenuCleanup(void) {
    DestroyText(&game_text);
}
