#ifndef INTRO_H
#define INTRO_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool IntroInit(SDL_GPUDevice *pGPUDevice);

/* Render the intro.
 * Returns true if all went well.
 */
bool IntroRender(void);

void IntroKeyDown(SDL_Scancode scancode);
void IntroKeyUp(SDL_Scancode scancode);

/* Clean everything up. */
void IntroCleanup(void);

#endif
