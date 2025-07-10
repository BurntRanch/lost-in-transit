#ifndef BUTTON_H
#define BUTTON_H

#include "common.h"
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <stdbool.h>

struct LE_Button {
    struct LE_RenderElement *element;

    bool hovered;
    bool held;

    /* Percentage value from 0 -> 1. Transitions everytime the button is hovered/unhovered. This value influences `angle` which is the actual value you should use. */
    float angle_perc;

    float angle;

    /* The highest angle this button can go. Basically defines what button_angle should be if `angle_perc` == 100%. If 0 then the behavior is disabled. */
    float max_angle;

    /* The color mod that should be applied to element->texture when it's inactive (not held, not hovered) */
    struct SDL_Color inactive_color_mod;

    /* If this is NULL, ButtonStep will return false on press. */
    void (*on_button_pressed)();
};

/* Initialize an LE_Button struct with default values. Please call this first. */
void InitButton(struct LE_Button * const pLEButton);

/* Steps in the button logic. */
bool ButtonStep(struct LE_Button * const pLEButton, const struct MouseInfo * const pMouseInfo, const double * const pDelta);

#endif