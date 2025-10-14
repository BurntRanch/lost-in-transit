#include "scenes/options.h"
#include "button.h"
#include "engine.h"
#include "options.h"
#include "scenes.h"
#include "label.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
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

static struct LE_Label cam_sens_label;
static struct LE_RenderElement cam_sens_element;

static struct SDL_Texture *slider_cam_sens_texture;
static struct LE_RenderElement slider_cam_sens_element;

static struct SDL_Texture *slider_cam_sens_button_texture;
static struct LE_RenderElement slider_cam_sens_button_element;
static struct LE_Button slider_cam_sens_button;

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static inline void BackButtonPressed() {
    LELoadScene(SCENE_MAINMENU);
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

    // Camera Sensitivity
    cam_sens_label.text = "Camera Sensitivity";
    if (!UpdateText(&cam_sens_label)) {
        return false;
    }
    cam_sens_element.texture = &cam_sens_label.texture;
    cam_sens_element.dstrect.w = cam_sens_label.surface->w;
    cam_sens_element.dstrect.h = cam_sens_label.surface->h;

    if (!(slider_cam_sens_texture = IMG_LoadTexture(renderer, "images/slider.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load 'images/slider.png'! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    slider_cam_sens_element.texture = &slider_cam_sens_texture;
    slider_cam_sens_element.dstrect.h = slider_cam_sens_texture->h;

    if (!(slider_cam_sens_button_texture = IMG_LoadTexture(renderer, "images/circle.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load 'images/circle.png'! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    slider_cam_sens_button_element.texture = &slider_cam_sens_button_texture;
    slider_cam_sens_button_element.dstrect.w = slider_cam_sens_button_texture->w;
    slider_cam_sens_button_element.dstrect.h = slider_cam_sens_button_texture->h;
    slider_cam_sens_button_element.dstrect.x = (slider_cam_sens_element.dstrect.w / 2);

    InitButton(&slider_cam_sens_button);
    slider_cam_sens_button.max_angle = 0.f;
    slider_cam_sens_button.element = &slider_cam_sens_button_element;

    InitButton(&checkbox_option_vsync_button);
    checkbox_option_vsync_button.max_angle = -10.0f;
    checkbox_option_vsync_button.element = &checkbox_option_vsync_element;
    checkbox_option_vsync_button.on_button_pressed = OptionVsyncButtonPressed;

    return true;
}

bool OptionsRender(void) {
    back_element.dstrect.x = LEScreenWidth * 0.0125f;
    back_element.dstrect.y = LEScreenHeight * 0.0125f;

    option_vsync_element.dstrect.x = LEScreenWidth * 0.1f;
    option_vsync_element.dstrect.y = LEScreenHeight * 0.05f + (back_element.dstrect.y + back_element.dstrect.h);
    checkbox_option_vsync_element.dstrect.x = option_vsync_element.dstrect.x + option_vsync_element.dstrect.w + 10;
    checkbox_option_vsync_element.dstrect.y = option_vsync_element.dstrect.y - 6;

    cam_sens_element.dstrect.x = LEScreenWidth * 0.1f;
    cam_sens_element.dstrect.y = LEScreenHeight * 0.05f + (option_vsync_element.dstrect.y + option_vsync_element.dstrect.h);

    slider_cam_sens_element.dstrect.x = SDL_max(SDL_max(LEScreenWidth * 0.1f - 30, 10), cam_sens_element.dstrect.x);
    slider_cam_sens_element.dstrect.y = cam_sens_element.dstrect.y + cam_sens_element.dstrect.h;
    slider_cam_sens_element.dstrect.w = LEScreenWidth * 0.5f - slider_cam_sens_element.dstrect.x;

    slider_cam_sens_button_element.dstrect.y = slider_cam_sens_element.dstrect.y;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&checkbox_option_vsync_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    ButtonStep(&slider_cam_sens_button, &mouse_info, &LEFrametime);

    if (slider_cam_sens_button.held) {
        options.cam_sens = SDL_max(SDL_min((mouse_info.x - slider_cam_sens_element.dstrect.x) / (slider_cam_sens_element.dstrect.w), 1.f), 0.f);
    }

    slider_cam_sens_button_element.dstrect.x = (slider_cam_sens_element.dstrect.x + 10) + (options.cam_sens * (slider_cam_sens_element.dstrect.w - 20)) - (slider_cam_sens_button_element.dstrect.w / 2);

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_element.dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTexture(renderer, *option_vsync_element.texture, NULL, &option_vsync_element.dstrect)) {
        fprintf(stderr, "Failed to render options text! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTexture(renderer, *cam_sens_element.texture, NULL, &cam_sens_element.dstrect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render camera sensitivity label! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTexture9Grid(renderer, *slider_cam_sens_element.texture, NULL, 30, 30, 0, 0, 0.0f, &slider_cam_sens_element.dstrect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render camera sensitivity slider! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTexture(renderer, *slider_cam_sens_button_element.texture, NULL, &slider_cam_sens_button_element.dstrect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render camera sensitivity slider button! (SDL Error: %s)\n", SDL_GetError());
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
    LEApplySettings();
}
