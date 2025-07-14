#ifndef COMMON_H
#define COMMON_H

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_render.h>
#include <stdbool.h>
#include <SDL3/SDL_rect.h>

#define LOCALHOST 2130706433
#define DEFAULT_PORT 63288

struct MouseInfo {
    float x, y;
    SDL_MouseButtonFlags state;
};

struct LE_RenderElement {
    /* Not used. It's the user's choice to use this variable to store a texture object (typically an SDL_Texture pointer) */
    SDL_Texture **texture;

    /* Where this element is to be rendered. */
    struct SDL_FRect dstrect;
};

/* https://en.wikipedia.org/wiki/Smoothstep */
static inline float smoothstep(float edge0, float edge1, float x)
{
    // Scale, bias and saturate x to 0..1 range
    x = SDL_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*(3 - 2 * x);
}

/* uh.. returns false if button_pressed == null and the button was pressed */
static inline bool activate_button_if_hovering(const int x, const int y, const bool mouse1_held, const struct SDL_FRect *bounds, bool * const button_active, bool * const button_held, void (* const button_pressed)()) {
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
                }
            } else if (*button_active) {
                *button_active = false;
                *button_held = false;
            }

    return true;
}

#endif