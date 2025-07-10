#include "button.h"
#include "common.h"

void InitButton(struct LE_Button * const pLEButton) {
    pLEButton->hovered = false;
    pLEButton->held = false;

    pLEButton->angle_perc = 0.0f;
    pLEButton->angle = 0.0f;

    pLEButton->max_angle = -5.0f;

    pLEButton->on_button_pressed = NULL;
}

bool ButtonStep(struct LE_Button * const pLEButton, const struct MouseInfo * const pMouseInfo, const double * const pDelta) {
    if (!activate_button_if_hovering(pMouseInfo->x, pMouseInfo->y, 
                                    pMouseInfo->state & SDL_BUTTON_LMASK,
                                    &pLEButton->element->dstrect,
                                    &pLEButton->hovered, &pLEButton->held, pLEButton->on_button_pressed))
            return false;
    
    if (pLEButton->hovered && pLEButton->angle_perc <= BUTTON_ANGLE_PERCENTAGE_MAX) {
        pLEButton->angle_perc += 3.125 * *pDelta;
    } else if (!pLEButton->hovered && pLEButton->angle_perc >= BUTTON_ANGLE_PERCENTAGE_MIN) {
        pLEButton->angle_perc -= 3.125 * *pDelta;
    }

    pLEButton->angle = smoothstep(0.f, 1.f, pLEButton->angle_perc)*pLEButton->max_angle;

    return true;
}
