#include "button.h"
#include "common.h"
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

static Sint8 selected_button_idx = -1;
static struct LE_Button *selected_button = NULL;
static bool selected_button_pressed = false;

void InitButton(struct LE_Button * const pLEButton) {
    pLEButton->hovered = false;
    pLEButton->held = false;

    pLEButton->force_hovered = false;

    pLEButton->angle_perc = 0.0f;
    pLEButton->angle = 0.0f;

    pLEButton->max_angle = -5.0f;

    pLEButton->inactive_color_mod = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };

    pLEButton->on_button_pressed = NULL;
}

void Navigate(struct LE_Button *const *ppLEButtons, size_t button_count, bool backward) {
    if (selected_button_idx >= 0) {
        ppLEButtons[selected_button_idx]->force_hovered = false;
    }

    if (backward) {
        selected_button_idx -= 1;
        if (selected_button_idx < 0) {
            selected_button_idx = button_count-1;
        }
    } else {
        selected_button_idx = (selected_button_idx + 1) % button_count;
    }

    (selected_button = ppLEButtons[selected_button_idx])->force_hovered = true;
}

void PressActiveButton() {
    selected_button_pressed = true;
}

void DisableNavigation() {
    selected_button_idx = -1;

    if (selected_button) {
        selected_button->force_hovered = false;
        selected_button = NULL;
    }
}

bool ButtonStep(struct LE_Button * const pLEButton, const struct MouseInfo * const pMouseInfo, const double * const pDelta) {
    if (!activate_button_if_hovering(pMouseInfo->x, pMouseInfo->y, 
                                    pMouseInfo->state & SDL_BUTTON_LMASK,
                                    &pLEButton->element->dstrect,
                                    &pLEButton->hovered, &pLEButton->held, pLEButton->on_button_pressed))
            return false;

    if (selected_button_pressed && selected_button) {
        selected_button_pressed = false;
        if (selected_button->on_button_pressed) {
            selected_button->on_button_pressed();
        } else {
            return false;
        }
    }
    
    if (pLEButton->hovered | pLEButton->force_hovered) {
        pLEButton->angle_perc = SDL_min(pLEButton->angle_perc + 3.125 * *pDelta, 1.0f);
    } else if (!pLEButton->hovered) {
        pLEButton->angle_perc = SDL_max(pLEButton->angle_perc - 3.125 * *pDelta, 0.0f);
    }

    pLEButton->angle = smoothstep(0.f, 1.f, pLEButton->angle_perc)*pLEButton->max_angle;

    if (pLEButton->element && pLEButton->element->texture && pLEButton->held) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, 200, 200, 200);
    } else if (pLEButton->element && pLEButton->element->texture && (pLEButton->hovered | pLEButton->force_hovered)) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, 255, 255, 255);
    } else if (pLEButton->element && pLEButton->element->texture) {
        SDL_SetTextureColorMod(*pLEButton->element->texture, pLEButton->inactive_color_mod.r, pLEButton->inactive_color_mod.g, pLEButton->inactive_color_mod.b);
    }

    return true;
}
