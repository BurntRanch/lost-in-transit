#include "play.h"
#include "common.h"
#include "engine.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>

static SDL_Renderer *renderer = NULL;

static struct SDL_Texture *back_texture = NULL;

static bool back_button_active = false;
static bool back_button_held = false;

static float back_button_angle_percentage = 0.0f;
static float back_button_angle = 0.0f;

static struct SDL_FRect back_dstrect = { 0, 0, 0, 0 };



static struct LE_Text host_text;

static bool host_button_active = false;
static bool host_button_held = false;

static float host_button_angle_percentage = 0.0f;
static float host_button_angle = 0.0f;

static struct SDL_FRect host_dstrect = { 0, 0, 0, 0 };



static struct LE_Text join_text;

static bool join_button_active = false;
static bool join_button_held = false;

static float join_button_angle_percentage = 0.0f;
static float join_button_angle = 0.0f;

static struct SDL_FRect join_dstrect = { 0, 0, 0, 0 };

bool PlayInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_dstrect.w = back_texture->w;
    back_dstrect.h = back_texture->h;

    host_text.text = "Host a server";
    host_text.fg = (SDL_Color) { 200, 100, 100, SDL_ALPHA_OPAQUE };
    host_text.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&host_text)) {
        return false;
    }
    host_dstrect.w = host_text.surface->w;
    host_dstrect.h = host_text.surface->h;

    join_text.text = "Join a server";
    join_text.fg = (SDL_Color) { 100, 100, 200, SDL_ALPHA_OPAQUE };
    join_text.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&join_text)) {
        return false;
    }
    join_dstrect.w = join_text.surface->w;
    join_dstrect.h = join_text.surface->h;

    return true;
}

/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_timer = 0.0;

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

static inline void HostButtonPressed() {
    LEScheduleLoadScene(SCENE_HOST);
}

static inline void JoinButtonPressed() {
    ;   /* idk */
}

bool PlayRender(const double * const delta) {
    fixed_update_timer += *delta;

    back_dstrect.x = LEScreenWidth * 0.0125;
    back_dstrect.y = LEScreenHeight * 0.0125;

    host_dstrect.x = SDL_max((LEScreenWidth * 0.25) - (host_dstrect.w / 2), 0);
    host_dstrect.y = SDL_max((LEScreenHeight * 0.5) - (host_dstrect.h / 2), 0);

    join_dstrect.x = SDL_max((LEScreenWidth * 0.75) - (join_dstrect.w / 2), 0);
    join_dstrect.y = SDL_max((LEScreenHeight * 0.5) - (join_dstrect.h / 2), 0);

    while (fixed_update_timer >= FIXED_UPDATE_TIME) {
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
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, &host_text,
                                    &host_dstrect, &host_button_active,
                                    &host_button_held, HostButtonPressed,
                                    (SDL_Color){ 200, 100, 100, SDL_ALPHA_OPAQUE }))
            return false;
        if (!activate_button_if_hovering(x, y,
                                    mouse1_held, &join_text,
                                    &join_dstrect, &join_button_active,
                                    &join_button_held, JoinButtonPressed,
                                    (SDL_Color){ 100, 100, 200, SDL_ALPHA_OPAQUE }))
            return false;

        if (back_button_active && back_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            back_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!back_button_active && back_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            back_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        if (host_button_active && host_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            host_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!host_button_active && host_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            host_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        if (join_button_active && join_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            join_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!join_button_active && join_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            join_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        back_button_angle = -smoothstep(0.f, 1.f, back_button_angle_percentage)*10;
        host_button_angle = -smoothstep(0.f, 1.f, host_button_angle_percentage)*BUTTON_ANGLE_MAX;
        join_button_angle = -smoothstep(0.f, 1.f, join_button_angle_percentage)*BUTTON_ANGLE_MAX;

        fixed_update_timer -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, back_texture, NULL, &back_dstrect, back_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, host_text.texture, NULL, &host_dstrect, host_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render host button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, join_text.texture, NULL, &join_dstrect, join_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render join button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void PlayCleanup(void) {
    SDL_DestroyTexture(back_texture);
    DestroyText(&host_text);
    DestroyText(&join_text);
}
