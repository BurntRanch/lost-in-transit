#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

extern TTF_Font* pLEGameFont;

extern int LEScreenWidth, LEScreenHeight;

extern double LEFrametime;

/* Only used in 3D accelerated scenes. */
extern SDL_GPUCommandBuffer* LECommandBuffer;
extern SDL_GPUTexture* LESwapchainTexture;
extern Uint32 LESwapchainWidth, LESwapchainHeight;

/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

/* So far, only initializes GameNetworkingSockets. This will change to initialize the full Steam API. (once I actually get my hands on it) */
bool LEInitSteam(void);

/* Loads a scene **immediately**, This is not safe to run multi-threaded or inside of a scenes render function. Consider calling LEScheduleLoadScene for that purpose instead.
 * Refer to scenes.h
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
 * Sets [LEFrametime] to the frametime in seconds.
 *
 * Returns false if the window is not initialized, something went wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(void);

/* Destroy the window. */
void LEDestroyWindow(void);
/* Unload/Cleanup the current scene. */
void LECleanupScene(void);

#endif
