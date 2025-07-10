#include "networking.h"

#include <stdio.h>

static void (*server_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*server_connect_callback)(const ConnectionHandle) = NULL;

static void (*client_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*client_connect_callback)(const ConnectionHandle) = NULL;

void NETSetServerDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const)) {
    server_disconnect_callback = pCallback;
}
void NETSetServerConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    server_connect_callback = pCallback;
}

void NETSetClientDisconnectCallback(void (*pCallback)(const ConnectionHandle, const char * const)) {
    client_disconnect_callback = pCallback;
}
void NETSetClientConnectCallback(void (*pCallback)(const ConnectionHandle)) {
    client_connect_callback = pCallback;
}

void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char * const pMessage) {
    if (role == NET_ROLE_SERVER) {
        fprintf(stderr, "Client (%d) is disconnecting! %s\n", handle, pMessage ? pMessage : "");
        
        if (server_disconnect_callback) {
            server_disconnect_callback(handle, pMessage);
        }
    } else {
        fprintf(stderr, "Disconnecting from server (%d)! %s\n", handle, pMessage ? pMessage : "");

        if (client_disconnect_callback) {
            client_disconnect_callback(handle, pMessage);
        }
    }
}

void NETHandleConnect(const enum Role role, const ConnectionHandle handle) {
    if (role == NET_ROLE_SERVER) {
        fprintf(stderr, "Client (%d) has connected!\n", handle);

        if (server_connect_callback) {
            server_connect_callback(handle);
        }
    } else {
        fprintf(stderr, "Connected to server (%d)!\n", handle);

        if (client_connect_callback) {
            client_connect_callback(handle);
        }
    }
}

void NETHandleConnectionFailure(const char * const pReason) {
    fprintf(stderr, "Failed to connect to server! (reason: %s)\n", pReason ? pReason : "unexpected error");

    if (client_disconnect_callback) {
        client_disconnect_callback(0, pReason);
    }
}
