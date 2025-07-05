#ifndef COMMON_H
#define COMMON_H

#include "engine.h"

#include <stdbool.h>
#include <SDL3/SDL_rect.h>

/* https://en.wikipedia.org/wiki/Smoothstep */
static inline float smoothstep(float edge0, float edge1, float x)
{
    // Scale, bias and saturate x to 0..1 range
    x = SDL_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*(3 - 2 * x);
}

/* uh.. returns false if button_pressed == null and the button was pressed */
static inline bool activate_button_if_hovering(const int x, const int y, const bool mouse1_held, struct LE_Text * const text, const struct SDL_FRect *bounds, bool * const button_active, bool * const button_held, void (* const button_pressed)(), const struct SDL_Color normal_fg) {
    if ((bounds->x <= x && x <= bounds->w + bounds->x) &&
            (bounds->y <= y && y <= bounds->h + bounds->y)) {
                if (!(*button_active) || (mouse1_held != *button_held)) {
                    if (*button_active && !mouse1_held) {
                        if (button_pressed)
                            button_pressed();
                        else
                            return false;
                    }

                    *button_active = true;
                    *button_held = mouse1_held;

                    if (text && *button_held) {
                        text->fg = (SDL_Color) { 200, 200, 200, SDL_ALPHA_OPAQUE };
                    } else if (text) {
                        text->fg = (SDL_Color) { 255, 255, 255, SDL_ALPHA_OPAQUE };
                    }

                    if (text && !UpdateText(text)) {
                        return false;
                    }
                }
            } else if (*button_active) {
                *button_active = false;

                if (text) {
                    text->fg = normal_fg;
                    if (!UpdateText(text)) {
                        return false;
                    }
                }
            }

    return true;
}

#endif