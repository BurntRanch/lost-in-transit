#ifndef CONNECT_H
#define CONNECT_H

#include <SDL3/SDL_render.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool ConnectInit(SDL_Renderer *pRenderer);

/* Render the connect menu.
 * Returns true if all went well.
 */
bool ConnectRender(void);

/* Clean everything up. */
void ConnectCleanup(void);

#endif