#include "networking.h"
#include "engine.h"
#include "scenes.h"
#include "steam.hh"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <cglm/quat.h>
#include <cglm/types.h>
#include <cglm/vec3.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* The ID that an administrator will have. Administrators can kick players and start games.
 * Clients will automatically request this ID, but because the ID system is server-authoritative, only the first client to ever connect will get this ID.
 */
#define ADMIN_ID 4500

#define DEFAULT_POS {0.f, 0.f, 0.f}
#define DEFAULT_ROT {0.f, 0.f, 0.f, 1.f}
#define DEFAULT_SCALE {1.f, 1.f, 1.f}

/* Sent by a client, echo'd out by the server. */
struct HelloPacket {
    enum PacketType type;

    /* From the server: "This is what you know me as", From the client: "This is what I know you as".
     * This is here because when we echo back, the client knows who we're talking to.
     */
    ConnectionHandle server_handle;

    int id;

    /* Ignored by the server, but picked up by the client. */
    vec3 position;
    vec4 rotation;
    vec3 scale;

    enum MovementDirection direction;
};

/* Sent by the server. */
struct DisconnectPacket {
    enum PacketType type;

    int id;
};

/* Sent by the client, and the server will only actually take action if the client is an administrator. */
struct RequestStartPacket {
    enum PacketType type;
};

/* Sent by the server. */
struct TransitionPacket {
    enum PacketType type;

    /* Which part of the game loop should the players go to? */
    enum TransDestination dest;
};

/* requested by a player */
struct MovementUpdatePacket {
    enum PacketType type;

    enum MovementDirection direction;
};

/* sent by the server when a players state updates */
struct PlayerUpdate {
    int id;

    vec3 position;
    vec4 rotation;
    vec3 scale;

    enum MovementDirection direction;
};

struct PlayerUpdatesPacket {
    enum PacketType type;

    /* amount of updates in this packet */
    size_t update_count;

    /* this packet is followed by PlayerUpdate structs. */
};

static void (*server_disconnect_callback)(const ConnectionHandle, const char *const) = NULL;
static void (*server_connect_callback)(const ConnectionHandle) = NULL;

static void (*client_disconnect_callback)(const ConnectionHandle, const char *const) = NULL;
static void (*client_update_callback)(ConnectionHandle, const struct Player *const) = NULL;
static void (*client_join_callback)(const ConnectionHandle, const struct Player *const) = NULL;
static void (*client_leave_callback)(const ConnectionHandle, int) = NULL;
static void (*client_connect_callback)(const ConnectionHandle) = NULL;

/* Our connection to the server. */
static ConnectionHandle client_connection;

static struct PlayersLinkedList *players = NULL;

/* who are we to the server? (in the players list) */
static struct Player *client_self;

static enum MovementDirection client_wanted_direction = MOVEMENT_COMPLETELY_STILL;

static enum TransDestination server_current_stage = TRANS_DEST_NONE;

/* Creates a linked list object. You can append this to another linked list and so on. */
static inline struct PlayersLinkedList *AllocPlayersLinkedList(const struct Player *const data) {
    struct PlayersLinkedList *linked_list = malloc(sizeof(struct PlayersLinkedList));

    linked_list->next = NULL;
    linked_list->prev = NULL;
    linked_list->ts = *data;

    return linked_list;
}

/* if second is NULL, it will not do anything. */
static inline void ConnectLinkedLists(struct PlayersLinkedList *first, struct PlayersLinkedList *second) {
    if (second == NULL) {
        return;
    }

    first->next = second;
    second->prev = first;
}

static inline struct PlayersLinkedList *FindPlayerByHandle(struct PlayersLinkedList *list, const ConnectionHandle handle) {
    if (!list) {
        return NULL;
    }

    while (list->prev != NULL) {
        list = list->prev;
    }

    while (list != NULL) {
        if (list->ts.handle == handle) {
            return list;
        }

        list = list->next;
    }

    return NULL;
}

static inline struct PlayersLinkedList *FindPlayerByID(struct PlayersLinkedList *list, const int id) {
    if (!list) {
        return NULL;
    }

    while (list->prev != NULL) {
        list = list->prev;
    }

    while (list != NULL) {
        if (list->ts.id == id) {
            return list;
        }

        list = list->next;
    }

    return NULL;
}

/* Frees a single list item. Doesn't free any other siblings */
static inline void FreeList(struct PlayersLinkedList *list) {
    if (!list) {
        return;
    }

    if (list->prev)
        list->prev->next = list->next;

    if (list->next)
        list->next->prev = list->prev;

    free(list);
}

/* Frees the list and any other siblings. */
static inline void FreeLists(struct PlayersLinkedList *list) {
    if (!list) {
        return;
    }

    while (list->prev != NULL) {
        list = list->prev;
    }

    while (list != NULL) {
        struct PlayersLinkedList *next = list->next;
        free(list);
        list = next;
    }
}

void NETSetServerDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char *const)) {
    server_disconnect_callback = pCallback;
}
void NETSetServerConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    server_connect_callback = pCallback;
}
void NETSetClientDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char *const)) {
    client_disconnect_callback = pCallback;
}
void NETSetClientConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    client_connect_callback = pCallback;
}
void NETSetClientJoinCallback(void (*pCallback)(const ConnectionHandle, const struct Player *const)) {
    client_join_callback = pCallback;
}
void NETSetClientLeaveCallback(void (*pCallback)(const ConnectionHandle, int)) {
    client_leave_callback = pCallback;
}
void NETSetClientUpdateCallback(void (*pCallback)(ConnectionHandle, const struct Player *const)) {
    client_update_callback = pCallback;
}

void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char *const pMessage) {
    switch (role) {
        case NET_ROLE_SERVER:
            struct PlayersLinkedList *player_list = FindPlayerByHandle(players, handle);

            if (player_list) {
                struct DisconnectPacket packet = {PACKET_TYPE_DISCONNECT, player_list->ts.id};

                SRSendMessageToClients(&packet, sizeof(packet));

                /* Make sure we don't leave behind a dangling pointer */
                if (players == player_list) {
                    players = player_list->next;
                }

                FreeList(player_list);
            }

            printf("[S]: Client (%d) is disconnecting! %s\n", handle, pMessage ? pMessage : "");

            if (server_disconnect_callback) {
                server_disconnect_callback(handle, pMessage);
            }
            break;
        case NET_ROLE_CLIENT:
            printf("[C]: Disconnecting from server (%d)! %s\n", handle, pMessage ? pMessage : "");

            if (client_disconnect_callback) {
                client_disconnect_callback(handle, pMessage);
            }
            break;
    }
}

void NETHandleConnect(const enum Role role, const ConnectionHandle handle) {
    switch (role) {
        case NET_ROLE_SERVER:
            printf("[S]: Client (%d) has connected!\n", handle);

            if (server_connect_callback) {
                server_connect_callback(handle);
            }
            break;
        case NET_ROLE_CLIENT:
            printf("[C]: Connected to server (%d)!\n", handle);

            client_connection = handle;

            /* watch how the server overrides our constant 4500 ID.
             * the model values (pos/rot/sca) are ignored by the server */
            struct HelloPacket packet = {PACKET_TYPE_HELLO, handle, 4500, DEFAULT_POS, DEFAULT_ROT, DEFAULT_SCALE, MOVEMENT_COMPLETELY_STILL};

            if (!SRSendToConnection(handle, &packet, sizeof(packet))) {
                /* :( */
                SRDisconnectFromServer();
                return;
            }

            if (client_connect_callback) {
                client_connect_callback(handle);
            }
            break;
    }
}

/* Prepends a player to the list..
 * There's no guarantee that the id will remain the same!! If a player already has the same ID, the ID will be changed to the first available ID.
 * In the rare case that we can't allocate an ID (if you can find 2147483647 players LMAO), this function will return null.
 */
static inline struct PlayersLinkedList *AddPlayer(const ConnectionHandle handle, int id) {
    if (players && FindPlayerByID(players, id)) {
        id = 0;
        while (FindPlayerByID(players, id) && id < INT_MAX) {
            id++;
        }

        if (id == INT_MAX) {
            return NULL;
        }
    }

    struct Player player = {handle, id, DEFAULT_POS, DEFAULT_ROT, DEFAULT_SCALE, MOVEMENT_COMPLETELY_STILL};

    struct PlayersLinkedList *player_list = AllocPlayersLinkedList(&player);

    ConnectLinkedLists(player_list, players);
    players = player_list;

    return player_list;
}

static inline void Server_HandleHelloPacket(const ConnectionHandle handle, const struct HelloPacket *const hello_packet) {
    struct PlayersLinkedList *player_list = AddPlayer(handle, hello_packet->id);

    if (!player_list) {
        /* there's no way we reached 2147483647 players */
        SRDisconnectClient(handle, "Sorry buddy, We already have 2147483647 players before you.\n(please report this bug thx XOXO --BurntRanch)");
        return;
    }

    struct PlayersLinkedList *last_list = players;
    while (last_list->next) {
        last_list = last_list->next;
    }

    struct HelloPacket packet = {PACKET_TYPE_HELLO, hello_packet->server_handle, player_list->ts.id,
                                    {*player_list->ts.position}, {*player_list->ts.rotation}, {*player_list->ts.scale}, player_list->ts.active_direction};
    /* send the player hello packets for every existing player so they catch up */
    while (last_list) {
        packet.server_handle = last_list->ts.handle;
        packet.id = last_list->ts.id;

        SDL_memcpy(packet.position, last_list->ts.position, sizeof(packet.position));
        SDL_memcpy(packet.rotation, last_list->ts.rotation, sizeof(packet.rotation));
        SDL_memcpy(packet.scale, last_list->ts.scale, sizeof(packet.scale));

        /* we want to send this one to **everybody** */
        if (last_list == player_list) {
            break;
        }

        if (!SRSendToConnection(handle, &packet, sizeof(packet))) {
            return;
        }

        last_list = last_list->prev;
    }

    packet.server_handle = hello_packet->server_handle;

    if (!SRSendMessageToClients(&packet, sizeof(packet))) {
        return; /* how */
    }

    printf("[S]: Player (Server Authoritative ID: %d) (Client Requested ID: %d) signed in!\n", player_list->ts.id, hello_packet->id);
}

static inline void Client_HandleHelloPacket(const ConnectionHandle handle, const struct HelloPacket *const hello_packet) {
    struct Player player = {hello_packet->server_handle, hello_packet->id, DEFAULT_POS, DEFAULT_ROT, DEFAULT_SCALE, hello_packet->direction};

    SDL_memcpy(player.position, hello_packet->position, sizeof(player.position));
    SDL_memcpy(player.rotation, hello_packet->rotation, sizeof(player.rotation));
    SDL_memcpy(player.scale, hello_packet->scale, sizeof(player.scale));

    /* If we are hosting an internal server, `players` is managed by the server, not the client part. */
    if (!SRIsHostingServer()) {
        struct PlayersLinkedList *player_list = AllocPlayersLinkedList(&player);
        ConnectLinkedLists(player_list, players);
        players = player_list;
    }
    
    if (hello_packet->server_handle == handle) {
        /* FindPlayerByID is guaranteed to succeed.
         * (in the extremely unlikely off-chance that it doesn't, a NULL pointer dereference is better than whatever mess is going on in the clients PC.) */
        client_self = &FindPlayerByID(players, hello_packet->id)->ts;

        SDL_Log("[C]: Server signed us in with ID %d!\n", hello_packet->id);
    } else {
        SDL_Log("[C]: Server signed someone else in with ID %d.\n", hello_packet->id);
    }


    if (client_join_callback) {
        client_join_callback(handle, &player);
    }
}

static inline void Client_HandleDisconnectPacket(const ConnectionHandle handle, const struct DisconnectPacket *const disconnect_packet) {
    if (client_leave_callback) {
        client_leave_callback(handle, disconnect_packet->id);
    }
}

static void HandlePacket(const enum Role role, const ConnectionHandle handle, const void *const data, const Uint32 size) {
    if (size < sizeof(enum PacketType)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[%c]: Received malformed packet! (size < uint32)\n", role == NET_ROLE_SERVER ? 'S' : 'C');
        return; /* why */
    }

    switch (*(enum PacketType *)data) {
        case PACKET_TYPE_HELLO:
            if (size < sizeof(struct HelloPacket)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[%c]: Received malformed packet! (size < sizeof(struct HelloPacket))\n", role == NET_ROLE_SERVER ? 'S' : 'C');
                return;
            }

            const struct HelloPacket *const hello_packet = data;

            switch (role) {
                case NET_ROLE_SERVER:
                    Server_HandleHelloPacket(handle, hello_packet);
                    break;

                /* The server will echo back our Hello packet, to confirm our sign-in. It may also change the ID */
                case NET_ROLE_CLIENT:
                    Client_HandleHelloPacket(handle, hello_packet);
                    break;
            }

            break;
        case PACKET_TYPE_DISCONNECT:
            /* only servers can issue disconnects */
            if (role == NET_ROLE_SERVER) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[S]: Received malformed packet! (disconnect packet sent to server)\n");
                return;
            }
            if (size < sizeof(struct DisconnectPacket)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (size < sizeof(struct DisconnectPacket))\n");
                return;
            }

            const struct DisconnectPacket *const disconnect_packet = data;

            /* this only comes from the server so we assume NET_ROLE_CLIENT */
            Client_HandleDisconnectPacket(handle, disconnect_packet);
            break;
        case PACKET_TYPE_REQUEST_START:
            /* only clients can issue start requests */
            if (role == NET_ROLE_CLIENT) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (start request packet sent by server)\n");
                return;
            }
            /* There's no data, all we need to know is that this client requested to start the game. */
            struct PlayersLinkedList *player = FindPlayerByHandle(players, handle);
            if (!player) {
                fprintf(stderr, "[S]: Unknown player tried starting the game!\n");
                return;
            }
            if (player->ts.id != ADMIN_ID) {
                fprintf(stderr, "[S]: Non-admin (%d) tried starting the game!\n", player->ts.id);
                return;
            }

            /* The administrator has requested to start the game. */
            server_current_stage = TRANS_DEST_INTRO;
            struct TransitionPacket packet = {PACKET_TYPE_TRANSITION, server_current_stage};
            if (!SRSendMessageToClients(&packet, sizeof(packet))) {
                fprintf(stderr, "[S]: Failed to send TransitionPacket to clients!\n");
                return;
            }

            break;
        case PACKET_TYPE_TRANSITION:
            /* only the server can issue transition updates */
            if (role == NET_ROLE_SERVER) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[S]: Received malformed packet! (transition packet sent to server)\n");
                return;
            }
            if (size < sizeof(struct TransitionPacket)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (size < sizeof(struct TransitionPacket))");
                return;
            }

            const struct TransitionPacket *transition_packet = data;

            switch (transition_packet->dest) {
                case TRANS_DEST_INTRO:
                    LEScheduleLoadScene(SCENE3D_INTRO);
                    break;
                default:
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (unknown destination)\n");
            }

            break;
        case PACKET_TYPE_PLAYER_UPDATE:
            /* only the server can issue player updates */
            if (role == NET_ROLE_SERVER) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[S]: Received malformed packet! (player update packet sent to server)\n");
                return;
            }
            if (size < sizeof(struct PlayerUpdatesPacket)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (size < sizeof(struct PlayerUpdatePacket))\n");
                return;
            }

            const struct PlayerUpdatesPacket *player_updates_packet = data;

            /* if this struct isn't big enough to store its updates.. */
            if (size < sizeof(struct PlayerUpdatesPacket) + (player_updates_packet->update_count * sizeof(struct PlayerUpdate))) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (size not big enough to hold updates)\n");
                return;
            }

            const struct PlayerUpdate *player_updates = data + offsetof(struct PlayerUpdatesPacket, update_count) + sizeof(player_updates_packet->update_count);

            for (size_t i = 0; i < player_updates_packet->update_count; i++) {
                static const struct PlayerUpdate *player_update;
                player_update = &player_updates[i];

                struct PlayersLinkedList *target_player = FindPlayerByID(players, player_update->id);

                if (!target_player) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Can't find target of player update!\n");
                    return;
                }

                /* We can kind of cheat this, because we're already updating this list in the internal server.
                 * This is also to prevent the client from interfering with the server. */
                if (!SRIsHostingServer()) {
                    SDL_memcpy(target_player->ts.position, player_update->position, sizeof(target_player->ts.position));
                    SDL_memcpy(target_player->ts.rotation, player_update->rotation, sizeof(target_player->ts.rotation));
                    SDL_memcpy(target_player->ts.scale, player_update->scale, sizeof(target_player->ts.scale));

                    target_player->ts.active_direction = player_update->direction;
                }

                if (client_update_callback) {
                    client_update_callback(handle, &target_player->ts);
                }
            }

            break;
        case PACKET_TYPE_MOVEMENT_UPDATE:
            if (role == NET_ROLE_CLIENT) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[C]: Received malformed packet! (movement update sent from server!)");
                return;
            }
            if (size < sizeof(struct MovementUpdatePacket)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[S]: Received malformed packet! (size < sizeof(struct MovementUpdatePacket))");
                return;
            }

            const struct MovementUpdatePacket *movement_update_packet = data;
            struct PlayersLinkedList *target_player = FindPlayerByHandle(players, handle);

            if (!target_player) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[S]: Can't find movement update packet target!\n");
                return;
            }

            /* TODO: some validation, i guess?
             * Would it even be necessary? If the player isn't allowed to move, the server wouldn't even attempt to move them. */

            target_player->ts.active_direction = movement_update_packet->direction;

            break;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[%c]: Received malformed packet! (invalid type)\n", role == NET_ROLE_SERVER ? 'S' : 'C');
            ;
    }
}

void NETHandleData(const enum Role role, const ConnectionHandle handle, const void *const data, const Uint32 size) {
    HandlePacket(role, handle, data, size);
}

enum MovementDirection NETGetDirection() {
    return client_self ? client_self->active_direction : MOVEMENT_COMPLETELY_STILL;
}

void NETChangeMovement(enum MovementDirection direction) {
    client_wanted_direction = direction;
}

static struct PlayerUpdate *server_player_updates = NULL;
static size_t server_player_updates_count = 0;

/* schedule an update for all clients (they'll be sent in bulk when FlushPlayerUpdates is called) */
static void AddPlayerUpdate(const struct Player *const player) {
    /* we're about to surpass a multiple of 32, resize the array. */
    if (server_player_updates_count % 32 == 0) {
        struct PlayerUpdate *new_array = SDL_malloc(sizeof(struct PlayerUpdate) * (int)SDL_ceilf((server_player_updates_count + 1) / 32.f) * 32);

        if (server_player_updates) {
            SDL_memcpy(new_array, server_player_updates, server_player_updates_count * sizeof(struct PlayerUpdate));

            SDL_free(server_player_updates);
        }

        server_player_updates = new_array;
    }

    struct PlayerUpdate *update = &server_player_updates[server_player_updates_count];
    *update = (struct PlayerUpdate){player->id, DEFAULT_POS, DEFAULT_ROT, DEFAULT_SCALE, player->active_direction};

    SDL_memcpy(update->position, player->position, sizeof(update->position));
    SDL_memcpy(update->rotation, player->rotation, sizeof(update->rotation));
    SDL_memcpy(update->scale, player->scale, sizeof(update->scale));

    server_player_updates_count++;
}

static void FlushPlayerUpdates() {
    size_t size = sizeof(struct PlayerUpdatesPacket) + (server_player_updates_count * sizeof(struct PlayerUpdate));

    struct PlayerUpdatesPacket *packet = SDL_malloc(size);
    packet->type = PACKET_TYPE_PLAYER_UPDATE;
    packet->update_count = server_player_updates_count;

    SDL_memcpy((void *)packet + sizeof(struct PlayerUpdatesPacket), server_player_updates, server_player_updates_count * sizeof(struct PlayerUpdate));

    if (server_player_updates) {
        SDL_free(server_player_updates);

        server_player_updates = NULL;
        server_player_updates_count = 0;
    }

    SRSendMessageToClients(packet, size);

    SDL_free(packet);
}

/* Move the player in their requested direction */
static inline void TickPlayerMovement(struct Player *const player) {
    vec3 direction;
    glm_vec3_zero(direction);
    
    if (player->active_direction & MOVEMENT_LEFT) {
        glm_vec3_add(direction, (vec3){0, 0, 1}, direction);
    }
    if (player->active_direction & MOVEMENT_RIGHT) {
        glm_vec3_add(direction, (vec3){0, 0, -1}, direction);
    }
    if (player->active_direction & MOVEMENT_FORWARD) {
        glm_vec3_add(direction, (vec3){-1, 0, 0}, direction);
    }
    if (player->active_direction & MOVEMENT_BACKWARD) {
        glm_vec3_add(direction, (vec3){1, 0, 0}, direction);
    }

    /* use 'rotation' to decide where the directions are (if we're looking up, 'forward' should be upward) */
//    static mat3 rot_matrix;
//    glm_quat_mat3(player->rotation, rot_matrix);
//    glm_vec3_rotate_m3(rot_matrix, direction, direction);
    glm_vec3_normalize(direction);

    glm_vec3_add(player->position, direction, player->position);
}

static inline void TickIntroStage() {
    struct PlayersLinkedList *head = players;

    for (; head; head = head->next) {
        TickPlayerMovement(&head->ts);

        AddPlayerUpdate(&head->ts);
    }
}

void NETTickServer() {
    switch (server_current_stage) {
        case TRANS_DEST_INTRO:
            TickIntroStage();
            break;
        default:
            ;
    }

    FlushPlayerUpdates();
}

void NETTickClient() {
    if (!client_self) {
        return;
    }

    /* our direction is correct */
    if (client_self->active_direction == client_wanted_direction) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Updating movement!\n");

    static struct MovementUpdatePacket packet = {PACKET_TYPE_MOVEMENT_UPDATE, MOVEMENT_COMPLETELY_STILL};
    packet.direction = client_wanted_direction;

    SRSendToConnection(client_connection, &packet, sizeof(packet));
}

const struct PlayersLinkedList *NETGetPlayers() {
    return players;
}

const struct Player *NETGetPlayerByID(int id) {
    struct PlayersLinkedList *player_list = FindPlayerByID(players, id);
    if (!player_list) {
        return NULL;
    }

    return &player_list->ts;
}

int NETGetSelfID() {
    return client_self ? client_self->id : -1;
}

void NETHandleConnectionFailure(const char *const pReason) {
    fprintf(stderr, "[C]: Failed to connect to server! (reason: %s)\n", pReason ? pReason : "unexpected error");

    if (client_disconnect_callback) {
        client_disconnect_callback(0, pReason);
    }
}

bool NETIsAdministrator() {
    return client_self && client_self->id == ADMIN_ID;
}

void NETRequestStart() {
    struct RequestStartPacket packet = {PACKET_TYPE_REQUEST_START};

    SRSendToConnection(client_connection, &packet, sizeof(packet));
}

void NETCleanupServer() {
    /* if we're not running a client then this list is going to be unused. */
    if (!SRIsConnectedToServer()) {
        FreeLists(players);
        players = NULL;
    }
}

void NETCleanupClient() {
    /* if we're not running a server then this list is going to be unused. */
    if (!SRIsHostingServer()) {
        FreeLists(players);
        players = NULL;
    }
    client_self = NULL;
}
