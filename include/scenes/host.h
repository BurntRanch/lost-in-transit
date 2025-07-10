#ifndef HOST_H
#define HOST_H

#include <SDL3/SDL_render.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool HostInit(SDL_Renderer *pRenderer);

/* Render the host menu.
 * Returns true if all went well.
 */
bool HostRender(void);

/* Clean everything up. */
void HostCleanup(void);

#endif