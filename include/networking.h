#ifndef NETWORKING_H
#define NETWORKING_H

#include <SDL3/SDL_stdinc.h>

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

#ifdef __cplusplus
extern "C" {
#endif
/* disconnect callbacks may receive a string containing a "reason" behind the disconnect. May be NULL (indicating a normal/requested disconnect)
 *
 * the handle may also be 0, if it's set to 0 that means that the failure (never a natural disconnect) happened before a connection could be established.
 */
void NETSetServerDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const));
void NETSetClientDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const));

void NETSetServerConnectCallback(void (*pCallback)(const ConnectionHandle));
void NETSetClientConnectCallback(void (*pCallback)(const ConnectionHandle));

/* Called by steam.cc, This handles a disconnect event at a high-level (high-level as in, letting other players know).
 *
 * If message is non-null, it may be displayed to the user. This is in the case where we have disconnected from a server that we were connected to earlier.
 */
void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char * const pMessage);

/* Called by steam.cc, This handles a connect event at a high-level (high-level as in, waiting for a "log-in" request and letting other players know) */
void NETHandleConnect(const enum Role role, const ConnectionHandle handle);

/* Called by steam.cc, only happens from the role of a client that failed to reach a server. in which case this would probably show an error to the user.
 *
 * This function will make no attempt to clean up anything. If the server was already previously connected, NETHandleDisconnect (with a message) should be called instead.
 * Speaking of showing errors, it will show the reason to the user. If the reason is null it's an "unexpected" error.
 */
void NETHandleConnectionFailure(const char * const pReason);

#ifdef __cplusplus
}
#endif

#endif