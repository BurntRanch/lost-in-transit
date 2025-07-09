#include "common.h"
#include "engine.h"
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

static struct SDL_Texture *back_texture = NULL;

static bool back_button_active = false;
static bool back_button_held = false;

static float back_button_angle_percentage = 0.0f;
static float back_button_angle = 0.0f;

static struct SDL_FRect back_dstrect = { 0, 0, 0, 0 };



static struct LE_Text ip_text;

static struct SDL_FRect ip_dstrect;



static struct SDL_Texture *copy_texture = NULL;

static bool copy_button_active = false;
static bool copy_button_held = false;

static float copy_button_angle_percentage = 0.0f;
static float copy_button_angle = 0.0f;

static struct SDL_FRect copy_dstrect = { 0, 0, 0, 0 };

bool HostInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }
    back_dstrect.w = back_texture->w;
    back_dstrect.h = back_texture->h;

    if (!(copy_texture = IMG_LoadTexture(renderer, "images/copy.png"))) {
        fprintf(stderr, "Failed to load 'images/copy.png'! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    ip = NULL;
    if (!(ip = SRStartServer(63288))) {
        return false;
    }

    ip_text.text = malloc(256);
    ip_text.text[0] = '\0';
    strncat(ip_text.text, "IP: ", 5);
    strncat(ip_text.text, ip, 128);

    ip_text.fg = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };
    ip_text.bg = (SDL_Color) { 0, 0, 0, SDL_ALPHA_TRANSPARENT };
    if (!UpdateText(&ip_text)) {
        return false;
    }
    ip_dstrect.w = ip_text.surface->w;
    ip_dstrect.h = ip_text.surface->h;

    /* Set the little copy button to be as big as the text. */
    copy_dstrect.h = ip_dstrect.h;
    copy_dstrect.w = copy_dstrect.h; /* Aspect ratio is always 1:1 (128x128) for the image, So we just set the width to the height of the text */

    return true;
}

/* time elapsed since last fixed update, updates every single render step. */
static double fixed_update_timer = 0.0;

/* Should we apply the "copied" status effect? */
static bool copy_button_apply_effect = false;

static inline void CopyButtonPressed() {
    SDL_SetClipboardText(ip);
    copy_button_apply_effect = !copy_button_apply_effect;
}

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

bool HostRender(const double * const delta) {
    fixed_update_timer += *delta;

    back_dstrect.x = LEScreenWidth * 0.0125;
    back_dstrect.y = LEScreenHeight * 0.0125;

    ip_dstrect.x = LEScreenWidth * 0.0125;
    ip_dstrect.y = back_dstrect.h + back_dstrect.y;
    
    copy_dstrect.x = ip_dstrect.w + ip_dstrect.x;
    copy_dstrect.y = ip_dstrect.y;

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

        if (!activate_button_if_hovering(x, y, 
                                        mouse1_held, NULL,
                                        &copy_dstrect,
                                        &copy_button_active, &copy_button_held, CopyButtonPressed,
                                        (SDL_Color) {0,0,0,0} ))
            return false;
        
        if (back_button_active && back_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            back_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!back_button_active && back_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            back_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }
        
        if (copy_button_active && copy_button_angle_percentage <= BUTTON_ANGLE_PERCENTAGE_MAX) {
            copy_button_angle_percentage += BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        } else if (!copy_button_active && copy_button_angle_percentage >= BUTTON_ANGLE_PERCENTAGE_MIN) {
            copy_button_angle_percentage -= BUTTON_ANGLE_PERCENTAGE_INCREMENT;
        }

        back_button_angle = -smoothstep(0.f, 1.f, back_button_angle_percentage)*10;
        copy_button_angle = -smoothstep(0.f, 1.f, copy_button_angle_percentage)*10;

        fixed_update_timer -= FIXED_UPDATE_TIME;
    }

    if (!SDL_RenderTexture(renderer, ip_text.texture, NULL, &ip_dstrect)) {
        fprintf(stderr, "Failed to render IP text! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, back_texture, NULL, &back_dstrect, back_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (copy_button_apply_effect) {
        if (!SDL_SetTextureColorMod(copy_texture, 20, 235, 20)) {
            fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    } else {
        if (!SDL_SetTextureColorMod(copy_texture, 255, 255, 255)) {
            fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    }

    if (!SDL_RenderTextureRotated(renderer, copy_texture, NULL, &copy_dstrect, copy_button_angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render copy button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

void HostCleanup(void) {
    DestroyText(&ip_text);
    free(ip_text.text);
    free(ip);

    SDL_DestroyTexture(back_texture);
    SDL_DestroyTexture(copy_texture);

    SRStopServer();
}
