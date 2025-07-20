#ifndef INTRO_H
#define INTRO_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>

/* Initialize elements such as text, etc.
 * Returns true on success.
 */
bool IntroInit(SDL_GPUDevice *pGPUDevice);

/* Render the intro.
 * Returns true if all went well.
 */
bool IntroRender(void);

/* Clean everything up. */
void IntroCleanup(void);

#endif