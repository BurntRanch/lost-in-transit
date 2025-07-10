#include "scenes/connect.h"
#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
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

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static struct LE_Label connection_status_label;
static struct SDL_FRect connection_status_dstrect;

static bool started = false;

static void SetConnectionStatusConnected(const ConnectionHandle _) {
    connection_status_label.text = "Connected!";
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    SDL_SetTextureColorMod(connection_status_label.texture, 100, 200, 100);
}
static void SetConnectionStatusDisconnected(const ConnectionHandle handle, const char * const pReason) {
    if (pReason) {
        size_t reason_size = SDL_strlen(pReason) + 1;

        connection_status_label.text = SDL_malloc(reason_size);
        SDL_memcpy(connection_status_label.text, pReason, reason_size);
    } else if (handle == 0) {
        connection_status_label.text = "Disconnected (unexpected error)";
    } else {
        connection_status_label.text = "Disconnected";
    }
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    SDL_SetTextureColorMod(connection_status_label.texture, 200, 100, 100);
}

static void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

bool ConnectInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;
    started = false;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_element.texture = &back_texture;
    back_element.dstrect.w = (*back_element.texture)->w;
    back_element.dstrect.h = (*back_element.texture)->h;

    InitButton(&back_button);
    back_button.max_angle = -10.0f;
    back_button.on_button_pressed = BackButtonPressed;
    back_button.element = &back_element;

    connection_status_label.text = "Connecting..";
    if (!UpdateText(&connection_status_label)) {
        return false;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    NETSetClientConnectCallback(SetConnectionStatusConnected);
    NETSetClientDisconnectCallback(SetConnectionStatusDisconnected);

    return true;
}

bool ConnectRender(void) {
    if (!started) {
        started = true;

        /* localhost:63288 */
        if (!SRConnectToServerIPv4(2130706433, 63288)) {
            return false;
        }
    }

    back_button.element->dstrect.x = LEScreenWidth * 0.0125;
    back_button.element->dstrect.y = LEScreenHeight * 0.0125;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_button.element->dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    connection_status_dstrect.x = LEScreenWidth * 0.0125;
    connection_status_dstrect.y = SDL_max(LEScreenHeight * 0.025, back_button.element->dstrect.y + back_button.element->dstrect.h);

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
