#include "scenes/connect.h"
#include "common.h"
#include "engine.h"
#include "networking.h"
#include "scenes.h"
#include "steam.hh"

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



static struct LE_Text connection_status_label;

static struct SDL_FRect connection_status_dstrect;

static bool started = false;

static void SetConnectionStatusConnected(const ConnectionHandle _) {
    connection_status_label.text = "Connected!";
    connection_status_label.fg = (SDL_Color) { 145, 255, 145, SDL_ALPHA_OPAQUE };
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;
}
static void SetConnectionStatusDisconnected(const ConnectionHandle handle, const char * const reason) {
    if (reason) {
        size_t reason_size = SDL_strlen(reason) + 1;

        connection_status_label.text = SDL_malloc(reason_size);
        SDL_memcpy(connection_status_label.text, reason, reason_size);
    } else if (handle == 0) {
        connection_status_label.text = "Disconnected (unexpected error)";
    } else {
        connection_status_label.text = "Disconnected";
    }
    connection_status_label.fg = (SDL_Color) { 255, 145, 145, SDL_ALPHA_OPAQUE };
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;
}

bool ConnectInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;
    started = false;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_dstrect.w = back_texture->w;
    back_dstrect.h = back_texture->h;

    connection_status_label.text = "Connecting..";
    connection_status_label.fg = (SDL_Color) { 200, 200, 200, SDL_ALPHA_OPAQUE };
    connection_status_label.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&connection_status_label)) {
        return false;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    NETSetClientConnectCallback(SetConnectionStatusConnected);
    NETSetClientDisconnectCallback(SetConnectionStatusDisconnected);

    return true;
}

static float fixed_update_timer = 0.0;

static void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

bool ConnectRender(const double * const delta) {
    fixed_update_timer += *delta;

    if (!started) {
        started = true;

        /* localhost:63288 */
        if (!SRConnectToServerIPv4(2130706433, 63288)) {
            return false;
        }
    }

    back_dstrect.x = LEScreenWidth * 0.0125;
    back_dstrect.y = LEScreenHeight * 0.0125;

    while (fixed_update_timer >= FIXED_UPDATE_TIME) {
        float x, y;

        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&x, &y);
        bool mouse1_held = mouse_state & SDL_BUTTON_LMASK;

        if (!activate_button_if_hovering(x, y, 
                                        mouse1_held, NULL,
                                        &back_dstrect,
                                        &back_button_active, &back_button_held, BackButtonPressed,
                                        (SDL_Color) {0,0,0,0} ))
            return false;
        
        if (back_button_active && back_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            back_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!back_button_active && back_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            back_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        back_button_angle = -smoothstep(0.f, 1.f, back_button_angle_percentage)*10;

        fixed_update_timer -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTextureRotated(renderer, back_texture, NULL, &back_dstrect, back_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    connection_status_dstrect.x = LEScreenWidth * 0.0125;
    connection_status_dstrect.y = SDL_max(LEScreenHeight * 0.025, back_dstrect.y + back_dstrect.h);

    if (!SDL_RenderTexture(renderer, connection_status_label.texture, NULL, &connection_status_dstrect)) {
        fprintf(stderr, "Failed to render connection status! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void ConnectCleanup(void) {
    SRDisconnectFromServer();
    DestroyText(&connection_status_label);
}
