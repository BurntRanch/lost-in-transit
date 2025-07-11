#pragma once
/* "there is also a plain C interface to facilitate binding to other languages." liar liar pants on fire */
/* a layer that abstracts C++ stuff from the rest of the C code and handles everything. */
/* This isn't a "wrapper" per-se, but rather a runtime specific to the engine. there are several key differences between the Steam API and these functions. */

#include "networking.h"

#include <SDL3/SDL_stdinc.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
    /* Initialize GameNetworkingSockets */
    bool SRInitGNS(void);

    /* Start listening on the port with GameNetworkingSockets.
     *
     * An error will have occurred when the resulting char string is NULL.
     * This library will only initialize a single server on a single port. If you try to call this function twice (even with different ports) you will get an error.
     *
     * Returns the IP address it's listening on.
     */
    char *SRStartServer(Uint16 port);

    /* Sends a single message (with a single copy!) to every client in the server */
    bool SRSendMessageToClients(void *data, const int size);

    /* Disconnect a client from your beautiful server. */
    void SRDisconnectClient(const ConnectionHandle handle, const char *pReason);

    void SRStopServer(void);

    bool SRConnectToServerIPv4(Uint32 ipv4, Uint16 port);
    bool SRConnectToServerIPv6(Uint8 *pIPv6, Uint16 port);

    void SRDisconnectFromServer(void);

    bool SRSendToConnection(const ConnectionHandle handle, const void * const data, const size_t size);

    /* poll both server & client connections, return true on success */
    bool SRPollConnections(void);
#ifdef __cplusplus
}
#endif
