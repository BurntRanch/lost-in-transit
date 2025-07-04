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

/*** Title Text ***/
static struct LE_Text title_text;

static SDL_FRect title_dstrect;

/*** Play Button/Text ***/
static struct LE_Text play_text;
static float play_text_angle = 0.f;

static bool play_button_active = false;
static bool play_button_held = false;

static float play_button_angle_percentage = 0.0f; /* Increases by 5% every 60th of a second. This variable basically defines how rotated the play text angle is, relative to -5 degrees. */

static SDL_FRect play_dstrect;

/*** Options Button/Text ***/
static struct LE_Text options_text;
static float options_text_angle = 0.f;

static bool options_button_active = false;
static bool options_button_held = false;
static float options_button_angle_percentage = 0.0f; /* Refer to play_button_angle_percentage comment. */

static SDL_FRect options_dstrect;

bool MainMenuInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!LEGameFont) {
        fprintf(stderr, "Trying to load text without loading the font!");
        return false;
    }

    title_text.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
    title_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    title_text.text = SDL_malloc(256);
    SDL_memcpy(title_text.text, "Lost in Transit", 16);
    if (!UpdateText(&title_text)) {
        return false;
    }

    title_dstrect.w = title_text.surface->w;
    title_dstrect.h = title_text.surface->h;

    play_text.fg = (SDL_Color){ 200, 140, 140, SDL_ALPHA_OPAQUE };
    play_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    play_text.text = "Play!";
    if (!UpdateText(&play_text)) {
        return false;
    }

    play_dstrect.w = play_text.surface->w;
    play_dstrect.h = play_text.surface->h;

    options_text.fg = (SDL_Color){ 140, 200, 140, SDL_ALPHA_OPAQUE };
    options_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    options_text.text = "Options";
    if (!UpdateText(&options_text)) {
        return false;
    }

    options_dstrect.w = options_text.surface->w;
    options_dstrect.h = options_text.surface->h;

    return true;
}

/* time elapsed since first render function (unused) */
// static double total_time = 0.0;
/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_counter = 0.0;

static inline void PlayButtonPressed() {
    LEScheduleLoadScene(SCENE_NONE);
}

static inline void OptionsButtonPressed() {
    LEScheduleLoadScene(SCENE_OPTIONS);
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

    play_dstrect.x = LEScreenWidth * 0.0225;
    play_dstrect.y = LEScreenHeight * 0.25;
    
    options_dstrect.x = LEScreenWidth * 0.0225;
    options_dstrect.y = LEScreenHeight * 0.35;

    while (fixed_update_counter >= FIXED_UPDATE_TIME) {
        /* Check if the user is hovering over play button */
        float x, y;

        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&x, &y);
        bool mouse1_held = mouse_state & SDL_BUTTON_LMASK;

        /* TODO: DRY (Don't Repeat Yourself), I have an idea for this. */

        /* If hovering, check if the play button is inactive. If it's inactive, reload the text with an active background. 
         * Otherwise, if the play button is active, reload the text with an inactive background.
         */
        if ((play_dstrect.x <= x && x <= play_dstrect.w + play_dstrect.x) &&
            (play_dstrect.y <= y && y <= play_dstrect.h + play_dstrect.y)) {
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

        /* Same thing as above but for options. */
        if ((options_dstrect.x <= x && x <= options_dstrect.w + options_dstrect.x) &&
            (options_dstrect.y <= y && y <= options_dstrect.h + options_dstrect.y)) {
                if (!options_button_active || (mouse1_held != options_button_held)) {
                    if (options_button_active && !mouse1_held) {
                        OptionsButtonPressed();
                    }

                    options_button_active = true;
                    options_button_held = mouse1_held;

                    if (options_button_held) {
                        options_text.fg = (SDL_Color){ 200, 200, 200, SDL_ALPHA_OPAQUE };
                    } else {
                        options_text.fg = (SDL_Color){ 255, 255, 255, SDL_ALPHA_OPAQUE };
                    }

                    if (!UpdateText(&options_text)) {
                        return false;
                    }
                }
            } else if (options_button_active) {
                options_button_active = false;

                options_text.fg = (SDL_Color){ 140, 200, 140, SDL_ALPHA_OPAQUE };
                if (!UpdateText(&options_text)) {
                    return false;
                }
            }
        
        if (play_button_active && play_button_angle_percentage <= 0.95f) {
            play_button_angle_percentage += 0.05f;
        } else if (!play_button_active && play_button_angle_percentage >= 0.05f) {
            play_button_angle_percentage -= 0.05f;
        }
        if (options_button_active && options_button_angle_percentage <= 0.95f) {
            options_button_angle_percentage += 0.05f;
        } else if (!options_button_active && options_button_angle_percentage >= 0.05f) {
            options_button_angle_percentage -= 0.05f;
        }

        play_text_angle = -smoothstep(0.f, 1.f, play_button_angle_percentage)*5;
        options_text_angle = -smoothstep(0.f, 1.f, options_button_angle_percentage)*5;

        fixed_update_counter -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, play_text.texture, NULL, &play_dstrect, play_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Play button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, options_text.texture, NULL, &options_dstrect, options_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Options button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    
    title_dstrect.x = LEScreenWidth * 0.0125;
    title_dstrect.y = LEScreenHeight * 0.0125;

    if (!SDL_RenderTexture(renderer, title_text.texture, NULL, &title_dstrect)) {
        fprintf(stderr, "Failed to draw the game title! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void MainMenuCleanup(void) {
    DestroyText(&title_text);
}
