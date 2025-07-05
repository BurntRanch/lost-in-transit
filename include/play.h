#ifndef PLAY_H
#define PLAY_H

#include <SDL3/SDL_render.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool PlayInit(SDL_Renderer *pRenderer);

/* Render the play menu.
 * Returns true if all went well.
 */
bool PlayRender(const double * const delta);

/* Clean everything up. */
void PlayCleanup(void);

#endif