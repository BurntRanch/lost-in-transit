#include "scenes/play.h"
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



static struct LE_Text connect_text;

static bool connect_button_active = false;
static bool connect_button_held = false;

static float connect_button_angle_percentage = 0.0f;
static float connect_button_angle = 0.0f;

static struct SDL_FRect connect_dstrect = { 0, 0, 0, 0 };

bool PlayInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_dstrect.w = back_texture->w;
    back_dstrect.h = back_texture->h;

    host_text.text = "Host server";
    host_text.fg = (SDL_Color) { 200, 100, 100, SDL_ALPHA_OPAQUE };
    host_text.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&host_text)) {
        return false;
    }
    host_dstrect.w = host_text.surface->w;
    host_dstrect.h = host_text.surface->h;

    connect_text.text = "Connect to server";
    connect_text.fg = (SDL_Color) { 100, 100, 200, SDL_ALPHA_OPAQUE };
    connect_text.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&connect_text)) {
        return false;
    }
    connect_dstrect.w = connect_text.surface->w;
    connect_dstrect.h = connect_text.surface->h;

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

static inline void ConnectButtonPressed() {
    LEScheduleLoadScene(SCENE_CONNECT);
}

bool PlayRender(const double * const delta) {
    fixed_update_timer += *delta;

    back_dstrect.x = LEScreenWidth * 0.0125;
    back_dstrect.y = LEScreenHeight * 0.0125;

    host_dstrect.x = SDL_max((LEScreenWidth * 0.25) - (host_dstrect.w / 2), 0);
    host_dstrect.y = SDL_max((LEScreenHeight * 0.5) - (host_dstrect.h / 2), 0);

    if (LEScreenWidth > 600) {
        connect_dstrect.x = SDL_max((LEScreenWidth * 0.75) - (connect_dstrect.w / 2), 0);
        connect_dstrect.y = SDL_max((LEScreenHeight * 0.5) - (connect_dstrect.h / 2), 0);
    } else {
        connect_dstrect.x = host_dstrect.x;
        connect_dstrect.y = host_dstrect.y + host_dstrect.h + 5;
    }

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
                                    mouse1_held, &connect_text,
                                    &connect_dstrect, &connect_button_active,
                                    &connect_button_held, ConnectButtonPressed,
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

        if (connect_button_active && connect_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            connect_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!connect_button_active && connect_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            connect_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        back_button_angle = -smoothstep(0.f, 1.f, back_button_angle_percentage)*10;
        host_button_angle = -smoothstep(0.f, 1.f, host_button_angle_percentage)*2;
        connect_button_angle = -smoothstep(0.f, 1.f, connect_button_angle_percentage)*2;

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

    if (!SDL_RenderTextureRotated(renderer, connect_text.texture, NULL, &connect_dstrect, connect_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render connect button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void PlayCleanup(void) {
    SDL_DestroyTexture(back_texture);
    DestroyText(&host_text);
    DestroyText(&connect_text);
}
