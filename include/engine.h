#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cglm/types.h>
#include <stdbool.h>

#include "model.h"

extern TTF_Font *pLEGameFont;

extern int LEScreenWidth, LEScreenHeight;

/* Relative X/Y mouse movement */
extern float LEMouseRelX, LEMouseRelY;

extern double LEFrametime;

/* Only used in 3D accelerated scenes. */
extern SDL_GPUCommandBuffer *LECommandBuffer;
extern SDL_GPUTexture *LESwapchainTexture, *LEDepthStencilTexture;
extern Uint32 LESwapchainWidth, LESwapchainHeight;

struct RenderInfo {
    /* Where is the camera? */
    vec3 cam_pos;
    /* Where is the camera facing? please normalize this. */
    vec3 dir_vec;

    SDL_GPUViewport viewport;
};

/* Apply settings, this doesn't need to be called except when you change a setting. */
void LEApplySettings(void);
/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

/* Transitions to a different scene.
 * Refer to scenes.h
 * Returns true on success.
 */
void LELoadScene(const Uint8 scene);

SDL_GPUDevice *LEGetGPUDevice();

enum PipelineSelection {
    PIPELINE_VERTEX_DEFAULT = 0x000001,
    PIPELINE_FRAG_TEXTURED_CEL = 0x001000,
    PIPELINE_FRAG_UNTEXTURED_CEL = 0x010000,
};

/* Choose a pipeline to initialize. Returns false on error. */
bool LEInitPipeline(struct GraphicsPipeline *pPipelineOut, enum PipelineSelection selection);

/* Prepare to render with the GPU by acquiring a command buffer, and storing it in [LECommandBuffer]. 
 * you're able to (and encouraged to) import scenes after calling this function but BEFORE calling LEStartGPURender */
bool LEPrepareGPURendering(void);

/* Starts the GPU Render pass. Don't import scenes and stuff while this is active. There's no LEStopGPURender function because it only ends at LEFinishGPURendering */
bool LEStartGPURender(void);

/* gets a pointer to the render info, which persists across LERenderModel calls.
 * feel free to modify, but don't free. */
struct RenderInfo *LEGetRenderInfo(void);

/* Renders a Scene3D.
 * You must make sure you call LEStartGPURendering before this function.
 * there's nothing wrong with not immediately calling LEFinishGPURendering afterwards but it's recommended.
 * Please don't call this on a Model more than once.
 * return false on failure. */
bool LERenderModel(struct Model *pScene3D);

/* Submit the command buffer and present the resulting texture to the renderer. */
bool LEFinishGPURendering(void);

void LEGrabMouse(void);
void LEReleaseMouse(void);

/* Render the current game state.
 *
 * Sets [LEFrametime] to the frametime in seconds.
 *
 * Returns false if the window is not initialized, something went wrong, or the user requested to exit (pressing escape).
 */
bool LEStepRender(void);

/* Destroy the GPU */
void LEDestroyGPU(void);
/* Destroy the window. */
void LEDestroyWindow(void);
/* Unload/Cleanup the current scene. */
void LECleanupScene(void);

#endif
