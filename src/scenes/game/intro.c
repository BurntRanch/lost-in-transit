#include "scenes/game/intro.h"
#include "engine.h"
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <assert.h>
#include <assimp/color4.h>
#include <assimp/defs.h>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/types.h>
#include <assimp/vector3.h>
#include <cglm/cam.h>
#include <cglm/cglm.h>
#include <cglm/affine.h>
#include <cglm/euler.h>
#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/types.h>
#include <cglm/vec2.h>
#include <cglm/vec3.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>

static SDL_GPUDevice *gpu_device = NULL;

static struct Scene3D *intro_scene = NULL;

static float camera_pitch, camera_yaw = 0;
static vec4 camera_rotation;

/* The direction the player is moving towards. */
static vec3 player_direction;

static struct RenderInfo *render_info;

const float speed = 10.0f;

bool IntroInit(SDL_GPUDevice *pGPUDevice) {
    gpu_device = pGPUDevice;

    LEGrabMouse();
    
    render_info = LEGetRenderInfo();
    render_info->viewport.x = 0;
    render_info->viewport.y = 0;
    render_info->viewport.min_depth = 0.f;
    render_info->viewport.max_depth = 1.f;

    camera_pitch = 0.f;
    camera_yaw = 0.f;

    glm_vec3_zero(player_direction);
    glm_vec3_zero(render_info->cam_pos);

    return true;
}

bool IntroRender(void) {
    if (!LEPrepareGPURendering()) {
        return false;
    }

    /* initialize scene if not initialized, exit on failure */
    if (!intro_scene) {
        if (!(intro_scene = LEImportScene3D("models/test.glb")) || !LEFinishGPURendering()) {
            return false;
        }
        return true;
    }

    render_info->viewport.w = LESwapchainWidth;
    render_info->viewport.h = LESwapchainHeight;
    if (!LEStartGPURender()) {
        return false;
    }

    camera_pitch = SDL_min(SDL_max(camera_pitch + -LEMouseRelY * LEFrametime, -1.15f), 0.8f);
    camera_yaw = SDL_fmodf(camera_yaw + -LEMouseRelX * LEFrametime, 6.28f);

    render_info->dir_vec[0] = 1.0f;
    render_info->dir_vec[1] = 0.0f;
    render_info->dir_vec[2] = 0.0f;
    glm_euler_xyz_quat((vec3){0.0f, camera_yaw, camera_pitch}, camera_rotation);
    glm_quat_rotatev(camera_rotation, render_info->dir_vec, render_info->dir_vec);

    static vec3 dir;
    glm_quat_rotatev(camera_rotation, player_direction, dir);
    glm_vec3_muladds(dir, LEFrametime, render_info->cam_pos);

    if (!LERenderScene3D(intro_scene)) {
        return false;
    }

    return LEFinishGPURendering();
}

void IntroKeyDown(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_W:
            player_direction[0] = SDL_min(player_direction[0] + speed, speed);
            break;
        case SDL_SCANCODE_A:
            player_direction[2] = SDL_max(player_direction[2] - speed, -speed);
            break;
        case SDL_SCANCODE_S:
            player_direction[0] = SDL_max(player_direction[0] - speed, -speed);
            break;
        case SDL_SCANCODE_D:
            player_direction[2] = SDL_min(player_direction[2] + speed, speed);
            break;
        default:
            ;
    }
}
void IntroKeyUp(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_W:
            player_direction[0] = SDL_max(player_direction[0] - speed, -speed);
            break;
        case SDL_SCANCODE_A:
            player_direction[2] = SDL_min(player_direction[2] + speed, speed);
            break;
        case SDL_SCANCODE_S:
            player_direction[0] = SDL_min(player_direction[0] + speed, speed);
            break;
        case SDL_SCANCODE_D:
            player_direction[2] = SDL_max(player_direction[2] - speed, -speed);
            break;
        default:
            ;
    }
}

void IntroCleanup(void) {
    LEReleaseMouse();

    LEDestroyScene3D(intro_scene);
    intro_scene = NULL;
}
