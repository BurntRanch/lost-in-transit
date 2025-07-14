#include "scenes/connect.h"
#include "button.h"
#include "common.h"
#include "engine.h"
#include "label.h"
#include "networking.h"
#include "scenes.h"
#include "steam.hh"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <stdio.h>

/* Linked list holding a players id and label */
struct PlayerLabelsList {
    struct PlayerLabelsList *prev;

    int id;
    struct LE_Label label;
    struct LE_RenderElement element;

    struct PlayerLabelsList *next;
};

static SDL_Renderer *renderer = NULL;

/* Box holding a list of players */
static struct SDL_Texture *box_texture;
static struct LE_RenderElement box_element;

/* The actual list of players */
static struct PlayerLabelsList *players_list;

static struct SDL_Texture *back_texture;
static struct LE_RenderElement back_element;
static struct LE_Button back_button;

static struct LE_Label connection_status_label;
static struct SDL_FRect connection_status_dstrect;

static bool started = false;
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

static void SetConnectionStatusConnected(const ConnectionHandle _) {
    connection_status_label.text = "Connected!";
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    SDL_SetTextureColorMod(connection_status_label.texture, 100, 200, 100);
}
static void SetConnectionStatusDisconnected(const ConnectionHandle handle, const char * const pReason) {
    if (pReason) {
        size_t reason_size = SDL_strlen(pReason) + 1;

        connection_status_label.text = SDL_malloc(reason_size);
        SDL_memcpy(connection_status_label.text, pReason, reason_size);
    } else if (handle == 0) {
        connection_status_label.text = "Disconnected (unexpected error)";
    } else {
        connection_status_label.text = "Disconnected";
    }
    if (!UpdateText(&connection_status_label)) {
        return;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    SDL_SetTextureColorMod(connection_status_label.texture, 200, 100, 100);
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

static void BackButtonPressed() {
    LEScheduleLoadScene(SCENE_PLAY);
}

bool ConnectInit(SDL_Renderer *pRenderer) {
    renderer = pRenderer;
    started = false;
    players_list = NULL;

    if (!(box_texture = IMG_LoadTexture(renderer, "images/box.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    box_element.texture = &box_texture;
    box_element.dstrect.w = (*box_element.texture)->w * 2;
    box_element.dstrect.h = (*box_element.texture)->h;

    if (!(back_texture = IMG_LoadTexture(renderer, "images/back.png"))) {
        fprintf(stderr, "Failed to load 'images/back.png'! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }
    back_element.texture = &back_texture;
    back_element.dstrect.w = (*back_element.texture)->w;
    back_element.dstrect.h = (*back_element.texture)->h;

    InitButton(&back_button);
    back_button.max_angle = -10.0f;
    back_button.on_button_pressed = BackButtonPressed;
    back_button.element = &back_element;

    connection_status_label.text = "Connecting..";
    if (!UpdateText(&connection_status_label)) {
        return false;
    }
    connection_status_dstrect.w = connection_status_label.surface->w;
    connection_status_dstrect.h = connection_status_label.surface->h;

    NETSetClientConnectCallback(SetConnectionStatusConnected);
    NETSetClientDisconnectCallback(SetConnectionStatusDisconnected);
    NETSetClientJoinCallback(AddPlayerToList);
    NETSetClientLeaveCallback(RemovePlayerFromList);

    return true;
}

bool ConnectRender(void) {
    if (should_quit) {
        return false;
    }

    if (!started) {
        started = true;

        /* localhost:63288 */
        if (!SRConnectToServerIPv4(2130706433, 63288)) {
            return false;
        }
    }

    box_element.dstrect.x = LEScreenWidth * 0.5 - box_element.dstrect.w * 0.5;
    box_element.dstrect.y = LEScreenHeight * 0.5 - box_element.dstrect.h * 0.5;

    back_button.element->dstrect.x = LEScreenWidth * 0.0125;
    back_button.element->dstrect.y = LEScreenHeight * 0.0125;

    struct MouseInfo mouse_info;
    mouse_info.state = SDL_GetMouseState(&mouse_info.x, &mouse_info.y);

    if (!ButtonStep(&back_button, &mouse_info, &LEFrametime)) {
        return false;
    }

    if (!SDL_RenderTextureRotated(renderer, *back_element.texture, NULL, &back_button.element->dstrect, back_button.angle, NULL, SDL_FLIP_NONE)) {
        fprintf(stderr, "Failed to render back button! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    connection_status_dstrect.x = LEScreenWidth * 0.0125;
    connection_status_dstrect.y = SDL_max(LEScreenHeight * 0.025, back_button.element->dstrect.y + back_button.element->dstrect.h);

    if (!SDL_RenderTexture(renderer, connection_status_label.texture, NULL, &connection_status_dstrect)) {
        fprintf(stderr, "Failed to render connection status! (SDL Error: %s)\n", SDL_GetError());
        return false;
    }

    if (!SDL_RenderTexture9Grid(renderer, *box_element.texture, NULL, 60, 60, 60, 60, 0.0f, &box_element.dstrect)) {
        fprintf(stderr, "Failed to draw box! (SDL Error: %s)\n", SDL_GetError());
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

    return true;
}

void ConnectCleanup(void) {
    while (players_list) {
        struct PlayerLabelsList *list = players_list;
        players_list = players_list->next;
        SDL_free(list);
    }

    SRDisconnectFromServer();
    DestroyText(&connection_status_label);
}
