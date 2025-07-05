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

#define BUTTON_ANGLE_PERCENTAGE_INCREMENT 0.05f
#define BUTTON_ANGLE_PERCENTAGE_MAX 1.0f - BUTTON_ANGLE_PERCENTAGE_INCREMENT
#define BUTTON_ANGLE_PERCENTAGE_MIN BUTTON_ANGLE_PERCENTAGE_INCREMENT

#define BUTTON_ANGLE_MAX 5

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

/*** Exit Button/Text ***/
static struct LE_Text exit_text;
static float exit_text_angle = 0.f;

static bool exit_button_active = false;
static bool exit_button_held = false;
static float exit_button_angle_percentage = 0.0f; /* Refer to play_button_angle_percentage comment. */

static SDL_FRect exit_dstrect;

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

    exit_text.fg = (SDL_Color){ 140, 140, 200, SDL_ALPHA_OPAQUE };
    exit_text.bg = (SDL_Color){ 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    exit_text.text = "Exit";
    if (!UpdateText(&exit_text)) {
        return false;
    }

    exit_dstrect.w = exit_text.surface->w;
    exit_dstrect.h = exit_text.surface->h;

    return true;
}

/* time elapsed since first render function (unused) */
// static double total_time = 0.0;

/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_timer = 0.0;

static inline void PlayButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

static inline void OptionsButtonPressed() {
    LEScheduleLoadScene(SCENE_OPTIONS);
}

/* https://en.wikipedia.org/wiki/Smoothstep */
static inline float smoothstep(float edge0, float edge1, float x)
{
    // Scale, bias and saturate x to 0..1 range
    x = SDL_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*(3 - 2 * x);
}

/* uh.. returns false if button_pressed == null and the button was pressed :3 */
static inline bool activate_button_if_hovering(const int x, const int y, const bool mouse1_held, struct LE_Text * const text, const struct SDL_FRect *bounds, bool * const button_active, bool * const button_held, void (* const button_pressed)(), const struct SDL_Color normal_fg) {
    if ((bounds->x <= x && x <= bounds->w + bounds->x) &&
            (bounds->y <= y && y <= bounds->h + bounds->y)) {
                if (!(*button_active) || (mouse1_held != *button_held)) {
                    if (*button_active && !mouse1_held) {
                        if (button_pressed)
                            button_pressed();
                        else
                            return false;
                    }

                    *button_active = true;
                    *button_held = mouse1_held;

                    if (*button_held) {
                        text->fg = (SDL_Color) { 200, 200, 200, SDL_ALPHA_OPAQUE };
                    } else {
                        text->fg = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };
                    }

                    if (!UpdateText(text)) {
                        return false;
                    }
                }
            } else if (*button_active) {
                *button_active = false;

                text->fg = normal_fg;
                if (!UpdateText(text)) {
                    return false;
                }
            }

    return true;
}

bool MainMenuRender(const double * const delta) {
    // total_time += *delta;
    fixed_update_timer += *delta;

    play_dstrect.x = LEScreenWidth * 0.0225;
    play_dstrect.y = LEScreenHeight * 0.25;
    
    options_dstrect.x = LEScreenWidth * 0.0225;
    options_dstrect.y = LEScreenHeight * 0.35;
    
    exit_dstrect.x = LEScreenWidth * 0.0225;
    exit_dstrect.y = LEScreenHeight * 0.45;

    while (fixed_update_timer >= FIXED_UPDATE_TIME) {
        /* Check if the user is hovering over play button */
        float x, y;

        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&x, &y);
        bool mouse1_held = mouse_state & SDL_BUTTON_LMASK;

        /* If hovering, check if the play button is inactive. If it's inactive, reload the text with an active background.
         * Otherwise, if the play button is active, reload the text with an inactive background.
         */
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, &play_text,
                                    &play_dstrect, &play_button_active,
                                    &play_button_held, PlayButtonPressed,
                                    (SDL_Color){ 200, 140, 140, SDL_ALPHA_OPAQUE }))
            return false;

        /* Same thing as above but for options. */
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, &options_text,
                                    &options_dstrect, &options_button_active,
                                    &options_button_held, OptionsButtonPressed,
                                    (SDL_Color){ 140, 200, 140, SDL_ALPHA_OPAQUE }))
            return false;

        /* Same thing as above but for exit. */
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, &exit_text,
                                    &exit_dstrect, &exit_button_active,
                                    &exit_button_held, NULL,
                                    (SDL_Color){ 140, 140, 200, SDL_ALPHA_OPAQUE }))
            return false;
        
        if (play_button_active && play_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            play_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!play_button_active && play_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            play_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        if (options_button_active && options_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            options_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!options_button_active && options_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            options_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        if (exit_button_active && exit_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            exit_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!exit_button_active && exit_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            exit_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        play_text_angle = -smoothstep(0.f, 1.f, play_button_angle_percentage)*BUTTON_ANGLE_MAX;
        options_text_angle = -smoothstep(0.f, 1.f, options_button_angle_percentage)*BUTTON_ANGLE_MAX;
        exit_text_angle = -smoothstep(0.f, 1.f, exit_button_angle_percentage)*BUTTON_ANGLE_MAX;

        fixed_update_timer -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, play_text.texture, NULL, &play_dstrect, play_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Play button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, options_text.texture, NULL, &options_dstrect, options_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Options button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, exit_text.texture, NULL, &exit_dstrect, exit_text_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Exit button! (SDL Error Code: %s)\n", SDL_GetError());
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
