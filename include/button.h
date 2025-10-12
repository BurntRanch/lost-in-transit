#ifndef BUTTON_H
#define BUTTON_H

#include "common.h"
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

struct LE_Button {
    struct LE_RenderElement *element;

    bool hovered;
    bool held;

    bool force_hovered;

    /* Percentage value from 0 -> 1. Transitions everytime the button is hovered/unhovered. This value influences `angle` which is the actual value you should use. */
    float angle_perc;

    float angle;

    /* The highest angle this button can go. Basically defines what button_angle should be if `angle_perc` == 100%. If 0 then the behavior is disabled. */
    float max_angle;

    /* The color mod that should be applied to element->texture when it's inactive (not held, not hovered) */
    struct SDL_Color inactive_color_mod;

    /* If this is NULL, ButtonStep will return false on press. */
    void (*on_button_pressed)(void);
};

/* Initialize an LE_Button struct with default values. Please call this first.
 * Also adds the button to the registry, to be used with Navigate() and similar functions.
 */
void InitButton(struct LE_Button *const pLEButton);

/* Navigate through all the buttons in the registry. Basically imitates TAB/S-TAB behavior. */
void Navigate(bool backward);

/* Activate a button that was activated by Navigate() */
void PressActiveButton(void);

/* Stop highlighting the button that the user navigated to with Navigate(). Basically imitates what happens when you move your mouse. */
void ResetNavigation(void);

/* Steps in the button logic. */
bool ButtonStep(struct LE_Button *const pLEButton, const struct MouseInfo *const pMouseInfo, const double *const pDelta);

/* Clears the button registry. */
void ClearButtonRegistry(void);

#endif
