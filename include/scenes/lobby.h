#ifndef HOST_H
#define HOST_H

#include <SDL3/SDL_render.h>

extern bool lobby_is_hosting;

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool LobbyInit(SDL_Renderer *pRenderer);

/* Render the lobby.
 * Returns true if all went well.
 */
bool LobbyRender(void);

/* Clean everything up. */
void LobbyCleanup(void);

#endif