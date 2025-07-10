#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
#include "scenes.h"
#include "steam.hh"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the IP address */
static char *ip = NULL;

static SDL_Renderer *renderer = NULL;

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static struct LE_Label ip_label;
static struct SDL_FRect ip_dstrect;

static struct SDL_Texture *copy_texture;
static struct LE_RenderElement copy_element;
static struct LE_Button copy_button;

/* Should we apply the "copied" status effect? (makes the button green) */
static bool copy_button_apply_effect = false;

static inline void CopyButtonPressed() {
    SDL_SetClipboardText(ip);
    copy_button_apply_effect = !copy_button_apply_effect;
}

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

bool HostInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

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

    if (!(copy_texture = IMG_LoadTexture(renderer, "images/copy.png"))) {
        fprintf(stderr, "Failed to load 'images/copy.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    copy_element.texture = &copy_texture;

    InitButton(&copy_button);
    copy_button.max_angle = -10.0f;
    copy_button.on_button_pressed = CopyButtonPressed;
    copy_button.element = &copy_element;

    ip = NULL;
    if (!(ip = SRStartServer(63288))) {
        return false;
    }

    ip_label.text = malloc(256);
    ip_label.text[0] = '\0';
    strncat(ip_label.text, "IP: ", 5);
    strncat(ip_label.text, ip, 128);


    if (!UpdateText(&ip_label)) {
        return false;
    }
    ip_dstrect.w = ip_label.surface->w;
    ip_dstrect.h = ip_label.surface->h;

    /* Set the little copy button to be as big as the text. */
    copy_element.dstrect.h = ip_dstrect.h;
    copy_element.dstrect.w = copy_element.dstrect.h; /* Aspect ratio is always 1:1 (128x128) for the image, So we just set the width to the height of the text */

    return true;
}

bool HostRender(void) {
    back_element.dstrect.x = LEScreenWidth * 0.0125;
    back_element.dstrect.y = LEScreenHeight * 0.0125;

    ip_dstrect.x = LEScreenWidth * 0.0125;
    ip_dstrect.y = back_element.dstrect.h + back_element.dstrect.y;
    
    copy_element.dstrect.x = ip_dstrect.w + ip_dstrect.x;
    copy_element.dstrect.y = ip_dstrect.y;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);
    
    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!ButtonStep(&copy_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTexture(renderer, ip_label.texture, NULL, &ip_dstrect)) {
        fprintf(stderr, "Failed to render IP text! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_element.dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (copy_button_apply_effect) {
        if (!SDL_SetTextureColorMod(*copy_element.texture, 20, 235, 20)) {
            fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    } else {
        if (!SDL_SetTextureColorMod(*copy_element.texture, 255, 255, 255)) {
            fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    }

    if (!SDL_RenderTextureRotated(renderer, *(SDL_Texture **)copy_element.texture, NULL, &copy_element.dstrect, copy_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render copy button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void HostCleanup(void) {
    DestroyText(&ip_label);
    free(ip_label.text);
    free(ip);

    SDL_DestroyTexture(*back_element.texture);
    SDL_DestroyTexture(*copy_element.texture);

    SRStopServer();
}
