#include "engine.h"
#include "label.h"
#include "button.h"
#include "common.h"
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_image/SDL_image.h>
#include <stddef.h>
#include <stdio.h>

static SDL_Renderer *renderer = NULL;

static bool running_dialog_box = false;
static bool dialog_box_statment = false;
static struct LE_Label dialog_box_main_label, dialog_box_true_label, dialog_box_false_label;
static struct SDL_Texture *dialog_box_texture;
static struct LE_RenderElement dialog_box_main_element, dialog_box_true_element, dialog_box_false_element;
static struct LE_Button dialog_box_true_button, dialog_box_false_button;

void DialogBoxTrueButtonPressed(void) {
    dialog_box_statment = true;
}

void DialogBoxFalseButtonPressed(void) {
    dialog_box_statment = false;
}

bool InitDialogBox(SDL_Renderer *pRenderer, const char *str, const char *true_str, const char *false_str)
{
    renderer = pRenderer; 
    running_dialog_box = true;

    const char *box_image = "images/box.png";
    const size_t true_str_len = SDL_strlen(true_str);
    const size_t false_str_len = SDL_strlen(false_str);

    // Initialize texts
    dialog_box_main_label.text = SDL_strdup(str);
    if (!UpdateText(&dialog_box_main_label)) {
        return false;
    }
    dialog_box_true_label.text = SDL_strdup(true_str);
    if (!UpdateText(&dialog_box_true_label)) {
        return false;
    }
    dialog_box_false_label.text = SDL_strdup(false_str);
    if (!UpdateText(&dialog_box_false_label)) {
        return false;
    }

    // Initialize main dialog box
    if (!(dialog_box_texture = IMG_LoadTexture(renderer, box_image))) {
        fprintf(stderr, "Failed to load '%s'! (SDL Error Code: %s)\n", box_image, SDL_GetError());
        return false;
    }
    dialog_box_main_element.texture = &dialog_box_texture;
    dialog_box_main_element.dstrect.w = LEScreenWidth / 1.2f;
    dialog_box_main_element.dstrect.h = LEScreenHeight / 1.2f;

    // Initialize true button box
    dialog_box_true_element.texture = &dialog_box_texture;
    dialog_box_true_element.dstrect.w = dialog_box_true_label.surface->w + true_str_len;
    dialog_box_true_element.dstrect.h = dialog_box_true_label.surface->h;

    InitButton(&dialog_box_true_button);
    dialog_box_true_button.element = &dialog_box_true_element;
    dialog_box_true_button.max_angle = -10.0f;
    dialog_box_true_button.on_button_pressed = DialogBoxTrueButtonPressed;

    // Initialize false button box
    dialog_box_false_element.texture = &dialog_box_texture;
    dialog_box_false_element.dstrect.w = dialog_box_false_label.surface->w + false_str_len;
    dialog_box_true_element.dstrect.h = dialog_box_false_label.surface->h;

    InitButton(&dialog_box_false_button);
    dialog_box_false_button.element = &dialog_box_false_element;
    dialog_box_false_button.max_angle = -10.0f;
    dialog_box_false_button.on_button_pressed = DialogBoxFalseButtonPressed;

    return true;
}

bool RenderDialogBox(void)
{
    if (!running_dialog_box)
        return false;

    dialog_box_main_element.dstrect.x = LEScreenWidth * 0.125f;
    dialog_box_main_element.dstrect.y = LEScreenHeight * 0.125f;

    dialog_box_true_element.dstrect.x = SDL_max((LEScreenWidth * 0.25) - (dialog_box_true_element.dstrect.w / 2), 0);
    dialog_box_true_element.dstrect.y = dialog_box_main_element.dstrect.y - 6;

    dialog_box_false_element.dstrect.x = SDL_max((LEScreenWidth * 0.25) - (dialog_box_false_element.dstrect.w / 2), 0);
    dialog_box_false_element.dstrect.y = dialog_box_true_element.dstrect.y;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&dialog_box_true_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&dialog_box_false_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTexture(renderer, *dialog_box_main_element.texture, NULL, &dialog_box_main_element.dstrect)) {
        fprintf(stderr, "Failed to render dialog box! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTextureRotated(renderer, *dialog_box_true_element.texture, NULL, &dialog_box_true_element.dstrect, dialog_box_true_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render dialog box true button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTextureRotated(renderer, *dialog_box_false_element.texture, NULL, &dialog_box_false_element.dstrect, dialog_box_false_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render dialog box false button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}
