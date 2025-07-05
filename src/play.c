#include "play.h"
#include "engine.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>

#define FIXED_UPDATE_TIME 0.016

#define BUTTON_ANGLE_PERCENTAGE_INCREMENT 0.05f
#define BUTTON_ANGLE_PERCENTAGE_MAX 1.0f - BUTTON_ANGLE_PERCENTAGE_INCREMENT
#define BUTTON_ANGLE_PERCENTAGE_MIN BUTTON_ANGLE_PERCENTAGE_INCREMENT

#define BUTTON_ANGLE_MAX 5

static SDL_Renderer *renderer = NULL;

static struct SDL_Texture *back_texture = NULL;

static bool back_button_active = false;
static bool back_button_held = false;

static float back_button_angle_percentage = 0.0f;
static float back_button_angle = 0.0f;

static struct SDL_FRect back_dstrect = { 0, 0, 0, 0 };

bool PlayInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

/* uh.. returns false if button_pressed == null and the button was pressed */
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

                    if (text && *button_held) {
                        text->fg = (SDL_Color) { 200, 200, 200, SDL_ALPHA_OPAQUE };
                    } else if (text) {
                        text->fg = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };
                    }

                    if (text && !UpdateText(text)) {
                        return false;
                    }
                }
            } else if (*button_active) {
                *button_active = false;

                if (text) {
                    text->fg = normal_fg;
                    if (!UpdateText(text)) {
                        return false;
                    }
                }
            }

    return true;
}

/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_timer = 0.0;

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

/* https://en.wikipedia.org/wiki/Smoothstep */
static inline float smoothstep(float edge0, float edge1, float x)
{
    // Scale, bias and saturate x to 0..1 range
    x = SDL_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*(3 - 2 * x);
}

bool PlayRender(const double * const delta) {
    fixed_update_timer += *delta;

    back_dstrect.x = LEScreenWidth * 0.0125;
    back_dstrect.y = LEScreenHeight * 0.0125;
    back_dstrect.w = back_texture->w;
    back_dstrect.h = back_texture->h;

    while (fixed_update_timer >= FIXED_UPDATE_TIME) {
        /* Check if the user is hovering over play button */
        float x, y;

        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&x, &y);
        bool mouse1_held = mouse_state & SDL_BUTTON_LMASK;

        /* check for clicks, hovers, and others. */
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, NULL,
                                    &back_dstrect, &back_button_active,
                                    &back_button_held, BackButtonPressed,
                                    (SDL_Color){ 0,0,0,0 }))
            return false;

        if (back_button_active && back_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            back_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!back_button_active && back_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            back_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        back_button_angle = -smoothstep(0.f, 1.f, back_button_angle_percentage)*BUTTON_ANGLE_MAX;

        fixed_update_timer -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, back_texture, NULL, &back_dstrect, back_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void PlayCleanup(void) {
    SDL_DestroyTexture(back_texture);
}
