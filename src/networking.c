#include "networking.h"
#include "steam.hh"

#include <SDL3/SDL_stdinc.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Sent by a client, echo'd out by the server. */
struct HelloPacket {
    enum PacketType type;

    /* From the server: "This is what you know me as", From the client: "This is what I know you as".
     * This is here because when we echo back, the client knows who we're talking to.
     */
    ConnectionHandle server_handle;

    int id;
};

/* Sent by a server. */
struct DisconnectPacket {
    enum PacketType type;

    int id;
};

struct PlayersLinkedList {
    /* Next element. NULL if this is the last element. */
    struct PlayersLinkedList *next;

    struct Player this;

    /* Previous element. NULL if this is the first element. */
    struct PlayersLinkedList *prev;
};

static void (*server_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*server_data_callback)(const ConnectionHandle, const void * const, const size_t) = NULL;
static void (*server_connect_callback)(const ConnectionHandle) = NULL;

static void (*client_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*client_data_callback)(const ConnectionHandle, const void * const, const size_t) = NULL;
static void (*client_join_callback)(const ConnectionHandle, const struct Player * const) = NULL;
static void (*client_leave_callback)(const ConnectionHandle, int) = NULL;
static void (*client_connect_callback)(const ConnectionHandle) = NULL;

/* who are we to the server? */
static struct Player client_self;

static struct PlayersLinkedList *server_players = NULL;

/* Creates a linked list object. You can append this to another linked list and so on. */
static inline struct PlayersLinkedList *AllocPlayersLinkedList(const struct Player * const data) {
    struct PlayersLinkedList *linked_list = malloc(sizeof(struct PlayersLinkedList));

    linked_list->next = NULL;
    linked_list->prev = NULL;
    linked_list->this = *data;

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
        if (list->this.handle == handle) {
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
        if (list->this.id == id) {
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
static inline void FreeLists(struct PlayersLinkedList * list) {
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

void NETSetServerDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const)) {
    server_disconnect_callback = pCallback;
}
void NETSetServerConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    server_connect_callback = pCallback;
}
void NETSetServerDataCallback(void (*pCallback)(const ConnectionHandle, const void *const, const size_t)) {
    server_data_callback = pCallback;
}

void NETSetClientDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const)) {
    client_disconnect_callback = pCallback;
}
void NETSetClientConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    client_connect_callback = pCallback;
}
void NETSetClientJoinCallback(void (*pCallback)(const ConnectionHandle, const struct Player * const)) {
    client_join_callback = pCallback;
}
void NETSetClientLeaveCallback(void (*pCallback)(const ConnectionHandle, int)) {
    client_leave_callback = pCallback;
}
void NETSetClientDataCallback(void (*pCallback)(const ConnectionHandle, const void *const, const size_t)) {
    client_data_callback = pCallback;
}

void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char * const pMessage) {
    struct PlayersLinkedList *player_list = FindPlayerByHandle(server_players, handle);

    if (player_list) {
        struct DisconnectPacket packet = { PACKET_TYPE_DISCONNECT, player_list->this.id };

        SRSendMessageToClients(&packet, sizeof(packet));
        
        /* Make sure we don't leave behind a dangling pointer */
        if (server_players == player_list) {
            server_players = player_list->next;
        }

        FreeList(player_list);
    }

    switch (role) {
        case NET_ROLE_SERVER:
            printf("Client (%d) is disconnecting! %s\n", handle, pMessage ? pMessage : "");
            
            if (server_disconnect_callback) {
                server_disconnect_callback(handle, pMessage);
            }
            break;
        case NET_ROLE_CLIENT:
            printf("Disconnecting from server (%d)! %s\n", handle, pMessage ? pMessage : "");

            if (client_disconnect_callback) {
                client_disconnect_callback(handle, pMessage);
            }
            break;
    }
}

void NETHandleConnect(const enum Role role, const ConnectionHandle handle) {
    switch (role) {
        case NET_ROLE_SERVER:
            printf("Client (%d) has connected!\n", handle);

            if (server_connect_callback) {
                server_connect_callback(handle);
            }
            break;
        case NET_ROLE_CLIENT:
            printf("Connected to server (%d)!\n", handle);

            client_self.handle = 0;

            /* watch how the server overrides our constant 4500 ID. */
            struct HelloPacket packet = { PACKET_TYPE_HELLO, handle, 4500 };

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
    if (server_players && FindPlayerByID(server_players, id)) {
        id = 0;
        while (FindPlayerByID(server_players, id) && id < INT_MAX) {
            id++;
        }

        if (id == INT_MAX) {
            return NULL;
        }
    }

    struct Player player = { handle, id };
    struct PlayersLinkedList *player_list = AllocPlayersLinkedList(&player);

    ConnectLinkedLists(player_list, server_players);
    server_players = player_list;

    return player_list;
}

static inline void Server_HandleHelloPacket(const ConnectionHandle handle, const struct HelloPacket * const hello_packet) {
    struct PlayersLinkedList *player_list = AddPlayer(handle, hello_packet->id);

    if (!player_list) {
        /* there's no way we reached 2147483647 players */
        SRDisconnectClient(handle, "Sorry buddy, We already have 2147483647 players before you.\n(please report this bug thx XOXO --BurntRanch)");
        return;
    }

    struct PlayersLinkedList *last_list = server_players;
    while (last_list->next) {
        last_list = last_list->next;
    }

    struct HelloPacket packet = { PACKET_TYPE_HELLO, hello_packet->server_handle, player_list->this.id };
    /* send the player hello packets for every existing player so they catch up */
    while (last_list) {
        packet.server_handle = last_list->this.handle;
        packet.id = last_list->this.id;

        /* we want to send this one to **everybody** */
        if (last_list == player_list) {
            break;
        }

        if (!SRSendToConnection(handle, &packet, sizeof(packet))) {
            return;
        }

        last_list = last_list->prev;
    }

    if (!SRSendMessageToClients(&packet, sizeof(packet))) {
        return; /* how */
    }

    printf("Player (Server Authoritative ID: %d) (Client Requested ID: %d) signed in!\n", player_list->this.id, hello_packet->id);
}

static inline void Client_HandleHelloPacket(const ConnectionHandle handle, const struct HelloPacket * const hello_packet) {
    if (hello_packet->server_handle == handle) {
        client_self.handle = handle;
        client_self.id = hello_packet->id;

        printf("Server signed us in with ID %d!\n", hello_packet->id);
    } else {
        printf("Server signed someone else in with ID %d.\n", hello_packet->id);
    }

    if (client_join_callback) {
        const struct Player player = { hello_packet->server_handle, hello_packet->id };

        client_join_callback(handle, &player);
    }
}

static inline void Client_HandleDisconnectPacket(const ConnectionHandle handle, const struct DisconnectPacket * const disconnect_packet) {
    if (client_leave_callback) {
        client_leave_callback(handle, disconnect_packet->id);
    }
}

static void HandlePacket(const enum Role role, const ConnectionHandle handle, const void * const data, const size_t size) {
    if (size < sizeof(enum PacketType)) {
        fprintf(stderr, "Received malformed packet! (size < uint32)\n");
        return; /* why */
    }

    switch (*(enum PacketType *)data) {
        case PACKET_TYPE_HELLO:
            if (size < sizeof(struct HelloPacket)) {
                fprintf(stderr, "Received malformed packet! (size < sizeof(struct HelloPacket))\n");
                return;
            }

            const struct HelloPacket * const hello_packet = data;

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
            if (size < sizeof(struct DisconnectPacket)) {
                fprintf(stderr, "Received malformed packet! (size < sizeof(struct DisconnectPacket))\n");
                return;
            }

            const struct DisconnectPacket * const disconnect_packet = data;

            /* this only comes from the server so we assume NET_ROLE_CLIENT */
            Client_HandleDisconnectPacket(handle, disconnect_packet);
            break;
        default:
            fprintf(stderr, "Received malformed packet! (invalid type)\n");
            ;
    }
}

void NETHandleData(const enum Role role, const ConnectionHandle handle, const void * const data, const size_t size) {
    HandlePacket(role, handle, data, size);

    switch (role) {
        case NET_ROLE_SERVER:
            printf("Received data from client %d!\n", handle);

            if (server_data_callback) {
                server_data_callback(handle, data, size);
            }
            break;
        case NET_ROLE_CLIENT:
            printf("Received data from the server!\n");

            if (client_data_callback) {
                client_data_callback(handle, data, size);
            }
            break;
    }
}

void NETHandleConnectionFailure(const char * const pReason) {
    fprintf(stderr, "Failed to connect to server! (reason: %s)\n", pReason ? pReason : "unexpected error");

    if (client_disconnect_callback) {
        client_disconnect_callback(0, pReason);
    }
}

void NETCleanup() {
    FreeLists(server_players);
    server_players = NULL;
}
