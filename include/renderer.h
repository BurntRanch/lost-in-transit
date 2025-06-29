#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>

/* Returns true on success. */
bool LEInitWindow(void);

/* Render the current game state.
 * Sets [frametime] to the frametime in milliseconds. If this is null, well, frametime data will go nowhere.
 * Returns false if the window is not initiialized, something went really wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(double *frametime);

/* Destroy the window. */
void LEDestroyWindow(void);

#endif
