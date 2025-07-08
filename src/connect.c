#include "connect.h"
#include "engine.h"
#include "networking.h"
#include "steam.hh"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <stdio.h>

static SDL_Renderer *renderer = NULL;

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

bool ConnectRender() {
    if (!started) {
        started = true;

        /* localhost:63288 */
        if (!SRConnectToServerIPv4(2130706433, 63288)) {
            return false;
        }
    }

    connection_status_dstrect.x = LEScreenWidth * 0.0125;
    connection_status_dstrect.y = LEScreenHeight * 0.0125;
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
