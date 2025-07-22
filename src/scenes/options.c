#include "scenes/options.h"
#include "button.h"
#include "engine.h"
#include "options.h"
#include "scenes.h"
#include "label.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3_image/SDL_image.h>
#include <stdio.h>

static SDL_Renderer *renderer = NULL;

static struct LE_Label option_vsync_label;
static struct LE_RenderElement option_vsync_element;

static struct SDL_Texture *checkbox_option_vsync_texture;
static struct LE_RenderElement checkbox_option_vsync_element;
static struct LE_Button checkbox_option_vsync_button;

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

const char *checkbox_image;

static inline void OptionVsyncButtonPressed() {
    options.vsync = !options.vsync;

    SDL_DestroyTexture(checkbox_option_vsync_texture);
    checkbox_image = options.vsync ? "images/checkbox_true.png" : "images/checkbox_false.png";
    if (!(checkbox_option_vsync_texture = IMG_LoadTexture(renderer, checkbox_image))) {
        fprintf(stderr, "Failed to load '%s'! (SDL Error Code: %s)\n", checkbox_image, SDL_GetError());
        return;
    }
    checkbox_option_vsync_element.texture = &checkbox_option_vsync_texture;
}

bool OptionsInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    // Back button
    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_element.texture = &back_texture;
    back_element.dstrect.w = (*back_element.texture)->w;
    back_element.dstrect.h = (*back_element.texture)->h;

    InitButton(&back_button);
    back_button.max_angle = -10.0f;
    back_button.element = &back_element;
    back_button.on_button_pressed = BackButtonPressed;

    // Vsync option
    option_vsync_label.text = "Vertical Sync";
    if (!UpdateText(&option_vsync_label)) {
        return false;
    }

    option_vsync_element.texture = &option_vsync_label.texture;
    option_vsync_element.dstrect.w = option_vsync_label.surface->w;
    option_vsync_element.dstrect.h = option_vsync_label.surface->h;

    // Check box (option_vsync)
    checkbox_image = options.vsync ? "images/checkbox_true.png" : "images/checkbox_false.png";
    if (!(checkbox_option_vsync_texture = IMG_LoadTexture(renderer, checkbox_image))) {
        fprintf(stderr, "Failed to load '%s'! (SDL Error Code: %s)\n", checkbox_image, SDL_GetError());
        return false;
    }
    checkbox_option_vsync_element.texture = &checkbox_option_vsync_texture;
    checkbox_option_vsync_element.dstrect.w = option_vsync_element.dstrect.h + 12;
    checkbox_option_vsync_element.dstrect.h = option_vsync_element.dstrect.h + 12;

    InitButton(&checkbox_option_vsync_button);
    checkbox_option_vsync_button.max_angle = -10.0f;
    checkbox_option_vsync_button.element = &checkbox_option_vsync_element;
    checkbox_option_vsync_button.on_button_pressed = OptionVsyncButtonPressed;

    return true;
}

bool OptionsRender(void) {
    back_element.dstrect.x = LEScreenWidth * 0.0125f;
    back_element.dstrect.y = LEScreenHeight * 0.0125f;

    option_vsync_element.dstrect.x = SDL_max((LEScreenWidth * 0.25) - (option_vsync_element.dstrect.w / 2), 0);
    option_vsync_element.dstrect.y = SDL_max((LEScreenHeight * 0.2) - (option_vsync_element.dstrect.h / 2), back_element.dstrect.y + back_element.dstrect.h);
    checkbox_option_vsync_element.dstrect.x = option_vsync_element.dstrect.x + option_vsync_element.dstrect.w + 10;
    checkbox_option_vsync_element.dstrect.y = option_vsync_element.dstrect.y - 6;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&checkbox_option_vsync_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_element.dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTexture(renderer, *option_vsync_element.texture, NULL, &option_vsync_element.dstrect)) {
        fprintf(stderr, "Failed to render options text! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTextureRotated(renderer, *checkbox_option_vsync_element.texture, NULL, &checkbox_option_vsync_element.dstrect, checkbox_option_vsync_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render checkbox_option_vsync button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void OptionsCleanup(void) {
    SDL_DestroyTexture(back_texture);
    SDL_DestroyTexture(checkbox_option_vsync_texture);
    DestroyText(&option_vsync_label);

    OverWriteConfigFile();
    LEInitWindow();
}
