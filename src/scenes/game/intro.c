#include "scenes/game/intro.h"
#include "engine.h"
#include "networking.h"
#include "scenes.h"
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_platform.h>
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
#include <stdlib.h>
#include <string.h>

struct PlayerSceneList {
    struct PlayerSceneList *prev;

    /* the scene that holds the players model
     * the root node is usually the target of any transform. */
    struct Scene3D *scene;
    
    int id;

    struct PlayerSceneList *next;
};

static SDL_GPUDevice *gpu_device = NULL;

static struct Scene3D *intro_scene = NULL;

static struct PlayerSceneList *player_scenes;

static float camera_pitch, camera_yaw = 0;

static struct RenderInfo *render_info;

static inline void AppendPlayerScene(struct PlayerSceneList *pPlayerScene) {
    if (!player_scenes) {
        player_scenes = pPlayerScene;
        return;
    }

    struct PlayerSceneList *head = player_scenes;
    while (head->next) {
        head = head->next;
    }

    head->next = pPlayerScene;
    pPlayerScene->prev = head;
}

/* Loads all the player models! */
static inline bool LoadScene() {
    const struct PlayersLinkedList *players = NETGetPlayers();

    if (!players) {
        /* Should be impossible.. */
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get players! no players found! (\?\?\?)\n");
        return false;
    }

    do {
        if (players->ts.id == NETGetSelfID()) {
            glm_vec3_copy((float *)players->ts.position, render_info->cam_pos);
            players = players->next;
            continue;
        }

        struct PlayerSceneList *player_obj = SDL_malloc(sizeof(struct PlayerSceneList));

        player_obj->prev = NULL;

        player_obj->scene = LEImportScene3D("models/character.glb");
        player_obj->id = players->ts.id;

        player_obj->next = NULL;

        size_t scene_object_count;
        struct Object *root_object = LEGetSceneObjects(player_obj->scene, &scene_object_count);

        SDL_assert(scene_object_count > 0);

        glm_vec3_copy((float *)players->ts.position, root_object->position);
        glm_vec4_copy((float *)players->ts.rotation, root_object->rotation);
        glm_vec3_copy((float *)players->ts.scale, root_object->scale);

        AppendPlayerScene(player_obj);

        players = players->next;
    } while (players);

    return true;
}

static void GoToMainMenu(const ConnectionHandle _, [[gnu::unused]] const char *const _pReason) {
    LEScheduleLoadScene(SCENE_MAINMENU);
}

static inline struct PlayerSceneList *GetPlayerSceneByID(int id) {
    if (!player_scenes) {
        return NULL;
    }

    struct PlayerSceneList *i = player_scenes;
    while (i && i->id != id) {
        i = i->next;
    }

    return i;
}

static void OnPlayerUpdate(const ConnectionHandle _, const struct Player * const player) {
    if (player->id == NETGetSelfID()) {
        glm_vec3_copy((float *)player->position, render_info->cam_pos);
        return;
    }
    
    const struct PlayerSceneList *player_scene = GetPlayerSceneByID(player->id);

    if (!player_scene) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Got a PlayerUpdate, but we couldn't find the player model!\n");
        return;
    }

    struct Object *root_object = LEGetSceneObjects(player_scene->scene, NULL);

    glm_vec3_copy((float *)player->position, root_object->position);
    glm_vec4_copy((float *)player->rotation, root_object->rotation);
    glm_vec3_copy((float *)player->scale, root_object->scale);
}

bool IntroInit(SDL_GPUDevice *pGPUDevice) {
    gpu_device = pGPUDevice;

    LEGrabMouse();
    
    render_info = LEGetRenderInfo();
    render_info->viewport.x = 0;
    render_info->viewport.y = 0;
    render_info->viewport.min_depth = 0.f;
    render_info->viewport.max_depth = 1.f;

    NETSetClientDisconnectCallback(GoToMainMenu);
    NETSetClientUpdateCallback(OnPlayerUpdate);

    return true;
}

bool IntroRender(void) {
    if (!LEPrepareGPURendering()) {
        return false;
    }

    /* initialize scene if not initialized, exit on failure */
    if (!intro_scene) {
        if (!(intro_scene = LEImportScene3D("models/test.glb")) || !LoadScene() || !LEFinishGPURendering()) {
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
    NETChangeCameraDirection((vec2){camera_pitch, camera_yaw});

    render_info->dir_vec[0] = 1.0f;
    render_info->dir_vec[1] = 0.0f;
    render_info->dir_vec[2] = 0.0f;
    if (NETGetSelfID() >= 0) {
        glm_quat_rotatev((float *)NETGetPlayerByID(NETGetSelfID())->rotation, render_info->dir_vec, render_info->dir_vec);
    }

    if (!LERenderScene3D(intro_scene) || !LEFinishGPURendering()) {
        return false;
    }

    return true;
}

void IntroCleanup(void) {
    LEReleaseMouse();

    for (; player_scenes; player_scenes = player_scenes->next) {
        if (player_scenes->prev) {
            SDL_free(player_scenes->prev);
        }
        LEDestroyScene3D(player_scenes->scene);
    }
    /* the loop above won't free the last element, so we have to do it manually */
    if (player_scenes) {
        SDL_free(player_scenes);
        player_scenes = NULL;
    }

    LEDestroyScene3D(intro_scene);
}
