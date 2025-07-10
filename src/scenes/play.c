#include "scenes/play.h"
#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
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

static struct LE_RenderElement back_texture;
static struct LE_Button back_button;

static struct LE_Label host_label;
static struct LE_RenderElement host_element;
static struct LE_Button host_button;

static struct LE_Label connect_label;
static struct LE_RenderElement connect_element;
static struct LE_Button connect_button;

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

static inline void HostButtonPressed() {
    LEScheduleLoadScene(SCENE_HOST);
}

static inline void ConnectButtonPressed() {
    LEScheduleLoadScene(SCENE_CONNECT);
}

bool PlayInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(back_texture.texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_texture.dstrect.w = ((SDL_Texture *)back_texture.texture)->w;
    back_texture.dstrect.h = ((SDL_Texture *)back_texture.texture)->h;

    InitButton(&back_button);
    back_button.max_angle = 10.0f;
    back_button.element = &back_texture;
    back_button.on_button_pressed = BackButtonPressed;

    host_label.text = "Host server";
    host_label.fg = (SDL_Color) { 200, 100, 100, SDL_ALPHA_OPAQUE };
    host_label.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&host_label)) {
        return false;
    }

    host_element.texture = &host_label.texture;
    host_element.dstrect.w = host_label.surface->w;
    host_element.dstrect.h = host_label.surface->h;

    InitButton(&host_button);
    host_button.max_angle = 2.5f;
    host_button.element = &host_element;
    host_button.on_button_pressed = HostButtonPressed;

    connect_label.text = "Connect to server";
    connect_label.fg = (SDL_Color) { 100, 100, 200, SDL_ALPHA_OPAQUE };
    connect_label.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&connect_label)) {
        return false;
    }

    connect_element.texture = &connect_label.surface;
    connect_element.dstrect.w = connect_label.surface->w;
    connect_element.dstrect.h = connect_label.surface->h;

    InitButton(&connect_button);
    connect_button.max_angle = 2.5f;
    connect_button.element = &connect_element;
    connect_button.on_button_pressed = ConnectButtonPressed;

    return true;
}

bool PlayRender(void) {
    back_texture.dstrect.x = LEScreenWidth * 0.0125;
    back_texture.dstrect.y = LEScreenHeight * 0.0125;

    host_element.dstrect.x = SDL_max((LEScreenWidth * 0.25) - (host_element.dstrect.w / 2), 0);
    host_element.dstrect.y = SDL_max((LEScreenHeight * 0.5) - (host_element.dstrect.h / 2), 0);

    if (LEScreenWidth > 600) {
        connect_element.dstrect.x = SDL_max((LEScreenWidth * 0.75) - (connect_element.dstrect.w / 2), 0);
        connect_element.dstrect.y = SDL_max((LEScreenHeight * 0.5) - (connect_element.dstrect.h / 2), 0);
    } else {
        connect_element.dstrect.x = host_element.dstrect.x;
        connect_element.dstrect.y = host_element.dstrect.y + host_element.dstrect.h + 5;
    }

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&host_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&connect_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, back_texture.texture, NULL, &back_texture.dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, host_element.texture, NULL, &host_element.dstrect, host_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render host button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, connect_element.texture, NULL, &connect_element.dstrect, connect_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render connect button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void PlayCleanup(void) {
    SDL_DestroyTexture(back_texture.texture);
    DestroyText(&host_label);
    DestroyText(&connect_label);
}
