#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

extern TTF_Font *LEGameFont;

extern int LEScreenWidth, LEScreenHeight;

struct LE_Text {
    /* The surface, usually created through functions like `TTF_RenderText_Shaded_Wrapped`. */
    SDL_Surface *surface;
    /* The texture, required to render. Just use SDL_CreateTextureFromSurface and all will go well! */
    SDL_Texture *texture;

    char *text;
    SDL_Color fg, bg;
};

/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

/* Free all resources related to an LE_Text struct 
 *
 * You can not render the LE_Text struct anymore, but you can run UpdateText on it again.
 */
void DestroyText(struct LE_Text * const pLEText);

/* Re-render an LE_Text struct with new text. 

 * Can be called even when the struct has not been rendered before.

 * Where do I set the text, or the fg/bg color? They're all in the struct, Set them before calling this function. 

 * Returns true on success.
 */
bool UpdateText(struct LE_Text * const pLEText);

/* Loads a scene. Refer to scenes.h
 *
 * Returns true on success.
 */
bool LELoadScene(const Uint8 scene);

/* Schedules a scene load. Refer to scenes.h
 *
 * The scene load will not take effect instantly. It will take effect at the start of the next frame.
 * Useful if a scene wants to safely change scenes during its render function.
 *
 * Only one scene load can take place at a time. The last call to this function will take precedence.
 *
 * Returns true on success.
 */
void LEScheduleLoadScene(const Uint8 scene);

/* Render the current game state.
 *
 * Sets [frametime] to the frametime in seconds. If this is null, well, frametime data will go nowhere.
 *
 * Returns false if the window is not initialized, something went wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(double *pFrameTime);

/* Destroy the window. */
void LEDestroyWindow(void);
/* Unload/Cleanup the current scene. */
void LECleanupScene(void);

#endif
