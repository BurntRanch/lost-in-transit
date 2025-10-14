#include "scenes/main_menu.h"

#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>

#define FIXED_UPDATE_TIME 0.016

static SDL_Renderer *renderer = NULL;

/*** Title Text ***/
static struct LE_Label title_label;
static SDL_FRect title_dstrect;

/*** Version Title Text ***/
static struct LE_Label version_title_label;
static SDL_FRect version_title_dstrect;

/*** Play Button/Text ***/
static struct LE_Label play_label;
static struct LE_RenderElement play_element;
static struct LE_Button play_button;

/*** Options Button/Text ***/
static struct LE_Label options_label;
static struct LE_RenderElement options_element;
static struct LE_Button options_button;

/*** Exit Button/Text ***/
static struct LE_Label exit_label;
static struct LE_RenderElement exit_element;
static struct LE_Button exit_button;

static inline void PlayButtonPressed() {
    LELoadScene(SCENE3D_INTRO);
}

static inline void OptionsButtonPressed() {
    LELoadScene(SCENE_OPTIONS);
}

bool MainMenuInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!pLEGameFont) {
        fprintf(stderr, "Trying to load text without loading the font!");
        return false;
    }

    title_label.text = "Lost in Transit";
    if (!UpdateText(&title_label)) {
        return false;
    }
    title_dstrect.w = title_label.surface->w;
    title_dstrect.h = title_label.surface->h;

    version_title_label.text = "v"LIT_VERSION;
    if (!UpdateText(&version_title_label)) {
        return false;
    }
    version_title_dstrect.w = version_title_label.surface->w / 1.4;
    version_title_dstrect.h = version_title_label.surface->h / 1.4;

    play_label.text = "Play!";
    if (!UpdateText(&play_label)) {
        return false;
    }
    play_element.texture = &play_label.texture;
    play_element.dstrect.w = play_label.surface->w;
    play_element.dstrect.h = play_label.surface->h;

    InitButton(&play_button);
    play_button.element = &play_element;
    play_button.on_button_pressed = PlayButtonPressed;
    play_button.inactive_color_mod = (SDL_Color){200, 100, 100, 0};

    options_label.text = "Options";
    if (!UpdateText(&options_label)) {
        return false;
    }

    options_element.texture = &options_label.texture;
    options_element.dstrect.w = options_label.surface->w;
    options_element.dstrect.h = options_label.surface->h;

    InitButton(&options_button);
    options_button.element = &options_element;
    options_button.on_button_pressed = OptionsButtonPressed;
    options_button.inactive_color_mod = (SDL_Color){100, 200, 100, 0};

    exit_label.text = "Exit";
    if (!UpdateText(&exit_label)) {
        return false;
    }

    exit_element.texture = &exit_label.texture;
    exit_element.dstrect.w = exit_label.surface->w;
    exit_element.dstrect.h = exit_label.surface->h;

    InitButton(&exit_button);
    exit_button.element = &exit_element;
    exit_button.inactive_color_mod = (SDL_Color){100, 100, 200, 0};

    return true;
}

bool MainMenuRender(void) {
    play_element.dstrect.x = LEScreenWidth * 0.0225;
    play_element.dstrect.y = LEScreenHeight * 0.25;

    options_element.dstrect.x = LEScreenWidth * 0.0225;
    options_element.dstrect.y = SDL_max(LEScreenHeight * 0.35, play_element.dstrect.y + play_element.dstrect.h + 5);

    exit_element.dstrect.x = LEScreenWidth * 0.0225;
    exit_element.dstrect.y = SDL_max(LEScreenHeight * 0.45, options_element.dstrect.y + options_element.dstrect.h + 5);

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&play_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&options_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (!ButtonStep(&exit_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, *(SDL_Texture **)play_element.texture, NULL, &play_element.dstrect, play_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Play button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTextureRotated(renderer, *(SDL_Texture **)options_element.texture, NULL, &options_element.dstrect, options_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Options button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_RenderTextureRotated(renderer, *(SDL_Texture **)exit_element.texture, NULL, &exit_element.dstrect, exit_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to draw the Exit button! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    title_dstrect.x = LEScreenWidth * 0.0125;
    title_dstrect.y = LEScreenHeight * 0.0125;

    if (!SDL_RenderTexture(renderer, title_label.texture, NULL, &title_dstrect)) {
        fprintf(stderr, "Failed to draw the game title! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    version_title_dstrect.x = LEScreenWidth  - version_title_dstrect.w - (LEScreenWidth  * 0.0125);
    version_title_dstrect.y = LEScreenHeight - version_title_dstrect.h - (LEScreenHeight * 0.0125);

    if (!SDL_RenderTexture(renderer, version_title_label.texture, NULL, &version_title_dstrect)) {
        fprintf(stderr, "Failed to draw the game version title! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void MainMenuCleanup(void) {
    DestroyText(&title_label);
    DestroyText(&version_title_label);
    DestroyText(&play_label);
    DestroyText(&options_label);
    DestroyText(&exit_label);

    ClearButtonRegistry();
}
