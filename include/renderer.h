#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

extern TTF_Font *pLEGameFont;

/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

/* Loads a scene. Refer to scenes.h
 *
 * Returns true on success.
 */
bool LELoadScene(const Uint8 scene);

/* Render the current game state.
 *
 * Sets [frametime] to the frametime in milliseconds. If this is null, well, frametime data will go nowhere.
 *
 * Returns false if the window is not initialized, something went wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(double *pFrameTime);

/* Destroy the window. */
void LEDestroyWindow(void);
/* Unload/Cleanup the current scene. */
void LECleanupScene(void);

#endif
