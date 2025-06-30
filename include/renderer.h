#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

extern TTF_Font *pLEGameFont;

/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

/* Render the current game state.
 * Sets [frametime] to the frametime in milliseconds. If this is null, well, frametime data will go nowhere.
 * Returns false if the window is not initiialized, something went really wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(double *frametime);

/* Destroy the window. */
void LEDestroyWindow(void);

#endif
