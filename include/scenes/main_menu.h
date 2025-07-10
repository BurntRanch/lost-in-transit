#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <SDL3/SDL_render.h>
#include <SDL3_ttf/SDL_ttf.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool MainMenuInit(SDL_Renderer *pRenderer);

/* Render the main menu.
 * Returns true if all went well.
 */
bool MainMenuRender(void);

/* Clean everything up. */
void MainMenuCleanup(void);

#endif