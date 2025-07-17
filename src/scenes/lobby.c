#include "scenes/lobby.h"
#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
#include "networking.h"
#include "scenes.h"
#include "steam.hh"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linked list holding a players id and label */
struct PlayerLabelsList {
    struct PlayerLabelsList *prev;

    int id;
    struct LE_Label label;
    struct LE_RenderElement element;

    struct PlayerLabelsList *next;
};

bool lobby_is_hosting = false;

/* the IP address */
static char *ip = NULL;

static SDL_Renderer *renderer = NULL;

/* Box holding a list of players */
static struct SDL_Texture *box_texture;
static struct LE_RenderElement box_element;

/* The actual list of players */
static struct PlayerLabelsList *players_list;

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static struct SDL_Texture *start_texture;
static struct LE_RenderElement start_element;
static struct LE_Button start_button;

static struct LE_Label status_label;
static struct SDL_FRect status_dstrect;

static struct SDL_Texture *copy_texture;
static struct LE_RenderElement copy_element;
static struct LE_Button copy_button;

/* Should we apply the "copied" status effect? (makes the button green) */
static bool copy_button_apply_effect = false;

static bool should_quit = false;

static struct PlayerLabelsList *AllocPlayerLabelsList() {
    struct PlayerLabelsList *list = SDL_malloc(sizeof(struct PlayerLabelsList));

    list->prev = NULL;

    list->id = 0;
    list->label.surface = NULL;
    list->label.texture = NULL;

    list->next = NULL;

    return list;
}

/* Disconnect list from its siblings */
static void DisconnectPlayerLabelsList(struct PlayerLabelsList *list) {
    if (!list) {
        return;
    }

    if (list->prev)
        list->prev->next = list->next;
    if (list->next)
        list->next->prev = list->prev;
}

/* first must be non-null, second can be null (nothing will happen) */
static void ConnectPlayerLabelsList(struct PlayerLabelsList *first, struct PlayerLabelsList *second) {
    if (!first) {
        return;
    }

    DisconnectPlayerLabelsList(second);

    if (second) {
        second->next = first->next;
        second->prev = first;
    }

    first->next = second;

    return;
}

static void FreePlayerLabelsList(struct PlayerLabelsList *list) {
    if (!list) {
        return;
    }

    DisconnectPlayerLabelsList(list);

    SDL_free(list);
}

/* Add a new player to the list we have. */
static void AddPlayerToList(const ConnectionHandle _, const struct Player *player) {
    struct PlayerLabelsList *list = AllocPlayerLabelsList();

    list->id = player->id;
    SDL_asprintf(&list->label.text, "ID: %d", player->id);
    if (!UpdateText(&list->label)) {
        should_quit = true;
        return;
    }
    list->element.texture = &list->label.texture;
    list->element.dstrect.w = list->label.surface->w;
    list->element.dstrect.h = list->label.surface->h;

    if (!players_list) {
        players_list = list;
    } else {
        struct PlayerLabelsList *last_list = players_list;

        while (last_list->next) {
            last_list = last_list->next;
        }

        ConnectPlayerLabelsList(last_list, list);
    }
}
static void RemovePlayerFromList(const ConnectionHandle _, int id) {
    struct PlayerLabelsList *list = players_list;
    while (list && list->id != id) {
        list = list->next;
    }

    if (!list) {
        return;
    }

    if (list == players_list) {
        players_list = players_list->next;
    }

    FreePlayerLabelsList(list);
}

static void UpdateStatusConnected(const ConnectionHandle _) {
    /* hide the label */
    status_dstrect.w = 0;
    status_dstrect.h = 0;
}
static void UpdateStatusDisconnected(const ConnectionHandle _, const char * const pReason) {
    if (pReason) {
        snprintf(status_label.text, 256, "%s", pReason);
    } else {
        sprintf(status_label.text, "Disconnected");
    }

    if (!UpdateText(&status_label)) {
        should_quit = true;
        fprintf(stderr, "Failed to update status label! (SDL Error: %s)\n", SDL_GetError());
        return;
    }
    status_dstrect.w = status_label.surface->w;
    status_dstrect.h = status_label.surface->h;

    if (!SDL_SetTextureColorMod(status_label.texture, 200, 100, 100)) {
        should_quit = true;
        fprintf(stderr, "Failed to set status label color mod! (SDL Error: %s)\n", SDL_GetError());
        return;
    }
}

static inline void CopyButtonPressed() {
    SDL_SetClipboardText(ip);
    copy_button_apply_effect = !copy_button_apply_effect;
}

static inline void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

static inline void StartButtonPressed() {
    NETRequestStart();
}

bool LobbyInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;

    if (!(box_texture = IMG_LoadTexture(renderer, "images/box.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s\n", SDL_GetError());
        return false;
    }
    box_element.texture = &box_texture;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s\n", SDL_GetError());
        return false;
    }
    back_element.texture = &back_texture;
    back_element.dstrect.w = back_texture->w;
    back_element.dstrect.h = back_texture->h;

    InitButton(&back_button);
    back_button.max_angle = -10.0f;
    back_button.on_button_pressed = BackButtonPressed;
    back_button.element = &back_element;

    if (!(start_texture = IMG_LoadTexture(renderer, "images/start.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s\n", SDL_GetError());
        return false;
    }
    start_element.texture = &start_texture;
    start_element.dstrect.w = start_texture->w;
    start_element.dstrect.h = start_texture->h;

    InitButton(&start_button);
    start_button.element = &start_element;
    start_button.on_button_pressed = StartButtonPressed;

    if (!(copy_texture = IMG_LoadTexture(renderer, "images/copy.png"))) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s\n", SDL_GetError());
        return false;
    }
    copy_element.texture = &copy_texture;

    InitButton(&copy_button);
    copy_button.max_angle = -10.0f;
    copy_button.on_button_pressed = CopyButtonPressed;
    copy_button.element = &copy_element;

    ip = NULL;

    status_label.text = malloc(256);
    sprintf(status_label.text, "Failed to connect!");

    /* The initial text depends on what we're doing. */
    if (lobby_is_hosting) {
        if ((ip = SRStartServer(DEFAULT_PORT)) && SRConnectToServerIPv4(LOCALHOST, DEFAULT_PORT)) {
            snprintf(status_label.text, 256, "IP: %s", ip);
        }

        if (!UpdateText(&status_label)) {
            return false;
        }
        status_dstrect.w = status_label.surface->w;
        status_dstrect.h = status_label.surface->h;

        /* Set the little copy button to be as big as the text. */
        copy_element.dstrect.h = status_dstrect.h;
        copy_element.dstrect.w = copy_element.dstrect.h; /* Aspect ratio is always 1:1 (128x128) for the image, So we just set the width to the height of the text */
    } else {
        if (SRConnectToServerIPv4(LOCALHOST, DEFAULT_PORT)) {
            sprintf(status_label.text, "Connecting..");
        }

        if (!UpdateText(&status_label)) {
            return false;
        }
        status_dstrect.w = status_label.surface->w;
        status_dstrect.h = status_label.surface->h;

        NETSetClientConnectCallback(UpdateStatusConnected);
        NETSetClientDisconnectCallback(UpdateStatusDisconnected);
    }

    NETSetClientJoinCallback(AddPlayerToList);
    NETSetClientLeaveCallback(RemovePlayerFromList);

    return true;
}

bool LobbyRender(void) {
    if (should_quit) {
        return false;
    }

    if (!SRIsConnectedToServer() && lobby_is_hosting) {
        SRStopServer();

        sprintf(status_label.text, "Failed to host! (Is there another instance running?)");
        if (!UpdateText(&status_label)) {
            return false;
        }

        status_dstrect.w = status_label.surface->w;
        status_dstrect.h = status_label.surface->h;
    }

    back_element.dstrect.x = LEScreenWidth * 0.0125;
    back_element.dstrect.y = LEScreenHeight * 0.0125;

    start_element.dstrect.x = LEScreenWidth - start_element.dstrect.w - (LEScreenWidth * 0.0125);
    start_element.dstrect.y = LEScreenHeight * 0.0125;

    status_dstrect.x = LEScreenWidth * 0.0125;
    status_dstrect.y = back_element.dstrect.h + back_element.dstrect.y;

    box_element.dstrect.w = SDL_min((*box_element.texture)->w * 2, LEScreenWidth - 10); /* Leave 10px (5px each side) of space as a minimum */
    box_element.dstrect.h = SDL_min((*box_element.texture)->h + (LEScreenHeight * 0.35), LEScreenHeight - (status_dstrect.y + status_dstrect.h) - 5);
    box_element.dstrect.x = LEScreenWidth * 0.5 - box_element.dstrect.w * 0.5;
    box_element.dstrect.y = SDL_max(LEScreenHeight * 0.5 - box_element.dstrect.h * 0.5, status_dstrect.y + status_dstrect.h);
    
    if (lobby_is_hosting) {
        copy_element.dstrect.x = status_dstrect.w + status_dstrect.x;
        copy_element.dstrect.y = status_dstrect.y;
    }

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);
    
    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }
    if (lobby_is_hosting) {
        if (!ButtonStep(&copy_button, &mouse_info, &LEFrametime)) {
            return false;
        }

        if (!ButtonStep(&start_button, &mouse_info, &LEFrametime)) {
            return false;
        }
    }

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_element.dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTexture(renderer, status_label.texture, NULL, &status_dstrect)) {
        fprintf(stderr, "Failed to render status label! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (lobby_is_hosting && SRIsHostingServer()) {
        if (copy_button_apply_effect) {
            if (!SDL_SetTextureColorMod(copy_texture, 20, 235, 20)) {
                fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
                return false;
            }
        } else {
            if (!SDL_SetTextureColorMod(copy_texture, 255, 255, 255)) {
                fprintf(stderr, "Failed to set texture color modulation! (SDL Error: %s)\n", SDL_GetError());
                return false;
            }
        }

        if (!SDL_RenderTextureRotated(renderer, copy_texture, NULL, &copy_element.dstrect, copy_button.angle, NULL, SDL_FLIP_NONE)) {
            fprintf(stderr, "Failed to render copy button! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }

        if (!SDL_RenderTextureRotated(renderer, start_texture, NULL, &start_element.dstrect, start_button.angle, NULL, SDL_FLIP_NONE)) {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render start button! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }
    }

    if (SRIsConnectedToServer()) {
        if (!SDL_RenderTexture9Grid(renderer, *box_element.texture, NULL, 60, 60, 60, 60, 0.0f, &box_element.dstrect)) {
            fprintf(stderr, "Failed to render box! (SDL Error: %s)\n", SDL_GetError());
            return false;
        }

        struct PlayerLabelsList *list = players_list;
        size_t i = 0;
        while (list) {
            list->element.dstrect.x = box_element.dstrect.x + 30;
            list->element.dstrect.y = (box_element.dstrect.y + 30) + (i * 40);
            if (!SDL_RenderTexture(renderer, *list->element.texture, NULL, &list->element.dstrect)) {
                fprintf(stderr, "Failed to draw player label! (SDL Error: %s)\n", SDL_GetError());
                return false;
            }

            list = list->next;
            i++;
        }
    }

    return true;
}

void LobbyCleanup(void) {
    while (players_list) {
        struct PlayerLabelsList *list = players_list;
        players_list = players_list->next;

        DestroyText(&list->label);

        SDL_free(list);
    }

    DestroyText(&status_label);
    free(status_label.text);
    free(ip);

    SDL_DestroyTexture(start_texture);
    SDL_DestroyTexture(box_texture);
    SDL_DestroyTexture(back_texture);
    SDL_DestroyTexture(copy_texture);
}
