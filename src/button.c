#include "button.h"
#include "common.h"
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

void InitButton(struct LE_Button * const pLEButton) {
    pLEButton->hovered = false;
    pLEButton->held = false;

    pLEButton->angle_perc = 0.0f;
    pLEButton->angle = 0.0f;

    pLEButton->max_angle = -5.0f;

    pLEButton->inactive_color_mod = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };

    pLEButton->on_button_pressed = NULL;
}

bool ButtonStep(struct LE_Button * const pLEButton, const struct MouseInfo * const pMouseInfo, const double * const pDelta) {
    if (!activate_button_if_hovering(pMouseInfo->x, pMouseInfo->y, 
                                    pMouseInfo->state & SDL_BUTTON_LMASK,
                                    &pLEButton->element->dstrect,
                                    &pLEButton->hovered, &pLEButton->held, pLEButton->on_button_pressed))
            return false;
    
    if (pLEButton->hovered) {
        pLEButton->angle_perc = SDL_min(pLEButton->angle_perc + 3.125 * *pDelta, 1.0f);
    } else if (!pLEButton->hovered) {
        pLEButton->angle_perc = SDL_max(pLEButton->angle_perc - 3.125 * *pDelta, 0.0f);
    }

    pLEButton->angle = smoothstep(0.f, 1.f, pLEButton->angle_perc)*pLEButton->max_angle;

    if (pLEButton->element && pLEButton->element->texture && pLEButton->held) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, 200, 200, 200);
    } else if (pLEButton->element && pLEButton->element->texture && pLEButton->hovered) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, 255, 255, 255);
    } else if (pLEButton->element && pLEButton->element->texture) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, pLEButton->inactive_color_mod.r, pLEButton->inactive_color_mod.g, pLEButton->inactive_color_mod.b);
    }

    return true;
}
