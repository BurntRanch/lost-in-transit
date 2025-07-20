#ifndef LABEL_H
#define LABEL_H

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <stdbool.h>

struct LE_Label {
    /* The surface, usually created through functions like `TTF_RenderText_Shaded_Wrapped`. */
    SDL_Surface* surface;
    /* The texture, required to render. Just use SDL_CreateTextureFromSurface and all will go well! */
    SDL_Texture* texture;

    char* text;
};

/* Free all resources related to an LE_Text struct
 *
 * You can not render the LE_Text struct anymore, but you can run UpdateText on it again.
 */
void DestroyText(struct LE_Label* const pLEText);

/* Re-render an LE_Text struct with new text.

 * Can be called even when the struct has not been rendered before.

 * Where do I set the text, or the fg/bg color? They're all in the struct, Set them before calling this function.

 * Returns true on success.
 */
bool UpdateText(struct LE_Label* const pLEText);

#endif