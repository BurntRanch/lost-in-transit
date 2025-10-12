#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cglm/types.h>
#include <stdbool.h>

extern TTF_Font *pLEGameFont;

extern int LEScreenWidth, LEScreenHeight;

/* Relative X/Y mouse movement */
extern float LEMouseRelX, LEMouseRelY;

extern double LEFrametime;

/* Only used in 3D accelerated scenes. */
extern SDL_GPUCommandBuffer *LECommandBuffer;
extern SDL_GPUTexture *LESwapchainTexture, *LEDepthStencilTexture;
extern Uint32 LESwapchainWidth, LESwapchainHeight;

/* An object in a Scene3D. */
struct Object {
    char *name;

    vec3 position;
    vec4 rotation;
    vec3 scale;

    struct Object *parent;

    struct Mesh *meshes;
    size_t mesh_count;

    /* don't use this, this is not reliable and only used internally for animations. */
    mat4 _transformation;
};

struct Scene3D;

struct RenderInfo {
    /* Where is the camera? */
    vec3 cam_pos;
    /* Where is the camera facing? please normalize this. */
    vec3 dir_vec;

    SDL_GPUViewport viewport;
};

/* Returns true on success. */
bool LEInitWindow(void);
/* Returns true on success. */
bool LEInitTTF(void);

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

/* Prepare to render with the GPU by acquiring a command buffer, and storing it in [LECommandBuffer]. 
 * you're able to (and encouraged to) import scenes after calling this function but BEFORE calling LEStartGPURender */
bool LEPrepareGPURendering(void);

/* Starts the GPU Render pass. Don't import scenes and stuff while this is active. There's no LEStopGPURender function because it only ends at LEFinishGPURendering */
bool LEStartGPURender(void);

/* Imports a GLTF 2.0 file as a Scene3D.
 * filename isn't sanitized
 * use LEDestroyScene3D to destroy this. */
struct Scene3D *LEImportScene3D(const char * const filename);

/* Gets an array of objects in a scene and sets [pCountOut] to the amount of objects there are (given it isn't NULL).
 * The first object is guaranteed to be the root node. Moving this moves everything.
 * Every object is guaranteed to come after its parent. */
struct Object *LEGetSceneObjects(const struct Scene3D * const pScene3D, size_t * const pCountOut);

/* gets a pointer to the render info, which persists across LERenderScene3D calls.
 * feel free to modify, but don't free. */
struct RenderInfo *LEGetRenderInfo(void);

/* Renders a Scene3D.
 * You must make sure you call LEStartGPURendering before this function.
 * there's nothing wrong with not immediately calling LEFinishGPURendering afterwards but it's recommended.
 * Please don't call this on a scene3D more than once.
 * return false on failure. */
bool LERenderScene3D(struct Scene3D *pScene3D);

/* Submit the command buffer and present the resulting texture to the renderer. */
bool LEFinishGPURendering(void);

void LEDestroyScene3D(struct Scene3D *pScene3D);

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
