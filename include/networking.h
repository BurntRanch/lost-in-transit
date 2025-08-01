#ifndef NETWORKING_H
#define NETWORKING_H

/* used by engine.c */
#define NETWORKING_TICKRATE 60

#include <SDL3/SDL_stdinc.h>
#include <cglm/types.h>

/* Think of this file as a list of high-level handlers, with steam.cc being a low-level handler of Steam connections. */
/* We'll define callbacks, track connections, and handle game events here. And if needed, we'll send over some commands to the engine (like change scenes). */

/* This is the equivalent to a HSteamNetConnection */
typedef Uint32 ConnectionHandle;

/* the role is who we are, If role = NET_ROLE_SERVER that means we are the server and we're handling something that happened with any client.
 * if role = NET_ROLE_CLIENT that means we are the client and we're handling something that happened with any server.
 */
enum Role {
    NET_ROLE_SERVER,
    NET_ROLE_CLIENT
};

/* bitmask representing movement direction */
enum MovementDirection {
    MOVEMENT_COMPLETELY_STILL = 0,
    MOVEMENT_FORWARD = 1,
    MOVEMENT_BACKWARD = 2,
    MOVEMENT_RIGHT = 4,
    MOVEMENT_LEFT = 8,
};

enum PacketType {
    PACKET_TYPE_HELLO,
    PACKET_TYPE_DISCONNECT,
    PACKET_TYPE_REQUEST_START,
    PACKET_TYPE_TRANSITION,
    PACKET_TYPE_PLAYER_UPDATE,
    PACKET_TYPE_MOVEMENT_UPDATE,
};

enum TransDestination {
    TRANS_DEST_NONE,
    TRANS_DEST_INTRO,
};

struct Player {
    ConnectionHandle handle;
    int id;

    vec3 position;
    vec4 rotation;
    vec3 scale;

    /* the direction this player is moving in. used to update the `position` value each tick. */
    enum MovementDirection active_direction;
};

struct PlayersLinkedList {
    /* Next element. NULL if this is the last element. */
    struct PlayersLinkedList *next;

    /* LMAO i had to do this because `this` is reserved by CPP */
    struct Player ts;

    /* Previous element. NULL if this is the first element. */
    struct PlayersLinkedList *prev;
};

#ifdef __cplusplus
extern "C" {
#endif
/* Network Callbacks
 *
 * Client callbacks will be reset to NULL when you call SRDisconnectFromServer. Similarly, Server callbacks will be reset to NULL when you call SRStopServer.
 * disconnect callbacks may receive a string containing a "reason" behind the disconnect. May be NULL (indicating a normal/requested disconnect).
 *
 * the handle may also be 0, if it's set to 0 that means that the failure (never a natural disconnect) happened before a connection could be established.
 *
 * While disconnect/connect callbacks only concern the caller and the other side, Join callbacks may concern another player in the session.
 * On the other hand, Leave callbacks **only** concern other players in the session. You'll receive a Disconnect callback if YOU are the one who left.
 */
void NETSetServerDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char *const));
void NETSetClientDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char *const));

void NETSetClientJoinCallback(void (*pCallback)(const ConnectionHandle, const struct Player *const));
void NETSetClientLeaveCallback(void (*pCallback)(const ConnectionHandle, int));

void NETSetClientUpdateCallback(void (*pCallback)(ConnectionHandle, const struct Player * const));

void NETSetServerConnectCallback(void (*pCallback)(const ConnectionHandle));
void NETSetClientConnectCallback(void (*pCallback)(const ConnectionHandle));

/* Called by steam.cc, This handles a disconnect event at a high-level (high-level as in, letting other players know).
 *
 * If message is non-null, it may be displayed to the user. This is in the case where we have disconnected from a server that we were connected to earlier.
 */
void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char *const pMessage);

/* Called by steam.cc, This handles a connect event at a high-level (high-level as in, waiting for a "log-in" request and letting other players know) */
void NETHandleConnect(const enum Role role, const ConnectionHandle handle);

/* Called by steam.cc */
void NETHandleData(const enum Role role, const ConnectionHandle handle, const void *const data, const Uint32 size);

/* Get the direction we're going at. completely still may be returned IF we aren't even connected */
enum MovementDirection NETGetDirection();

/* Ask the server to change our movement direction. */
void NETChangeMovement(enum MovementDirection direction);

void NETTickServer();
void NETTickClient();

const struct PlayersLinkedList *NETGetPlayers();

const struct Player *NETGetPlayerByID(int id);

/* For the client: Returns this client's ID to the server. */
int NETGetSelfID();

/* Called by steam.cc, only happens from the role of a client that failed to reach a server. in which case this would probably show an error to the user.
 *
 * This function will make no attempt to clean up anything. If the server was already previously connected, NETHandleDisconnect (with a message) should be called instead.
 * Speaking of showing errors, it will show the reason to the user. If the reason is null it's an "unexpected" error.
 */
void NETHandleConnectionFailure(const char *const pReason);

/* Check if we're an administrator. Shouldn't be called unless you're a client. */
bool NETIsAdministrator();

/* Ask the server to start the game. Only works if you're the administrator (Check with NETIsAdministrator). No guarantee of success. */
void NETRequestStart();

/* reset server state */
void NETCleanupServer();

/* reset client state */
void NETCleanupClient();

#ifdef __cplusplus
}
#endif

#endif
