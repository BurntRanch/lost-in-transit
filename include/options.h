#ifndef OPTIONS_H
#define OPTIONS_H

#include <SDL3/SDL_render.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool OptionsInit(SDL_Renderer *pRenderer);

/* Render the options menu.
 * Returns true if all went well.
 */
bool OptionsRender(const double * const delta);

/* Clean everything up. */
void OptionsCleanup(void);

#endif