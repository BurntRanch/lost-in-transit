#include "networking.h"

#include <stdio.h>

static void (*server_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*server_connect_callback)(const ConnectionHandle) = NULL;

static void (*client_disconnect_callback)(const ConnectionHandle, const char * const) = NULL;
static void (*client_connect_callback)(const ConnectionHandle) = NULL;

void NETSetServerDisconnectCallback(void (*callback)(const ConnectionHandle, const char * const)) {
    server_disconnect_callback = callback;
}
void NETSetServerConnectCallback(void (*callback)(const ConnectionHandle)) {
    server_connect_callback = callback;
}

void NETSetClientDisconnectCallback(void (*callback)(const ConnectionHandle, const char * const)) {
    client_disconnect_callback = callback;
}
void NETSetClientConnectCallback(void (*callback)(const ConnectionHandle)) {
    client_connect_callback = callback;
}

void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char * const message) {
    if (role == NET_ROLE_SERVER) {
        fprintf(stderr, "Client (%d) is disconnecting! %s\n", handle, message ? message : "");
        
        if (server_disconnect_callback) {
            server_disconnect_callback(handle, message);
        }
    } else {
        fprintf(stderr, "Disconnecting from server (%d)! %s\n", handle, message ? message : "");

        if (client_disconnect_callback) {
            client_disconnect_callback(handle, message);
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

void NETHandleConnectionFailure(const char * const reason) {
    fprintf(stderr, "Failed to connect to server! (reason: %s)\n", reason ? reason : "unexpected error");

    if (client_disconnect_callback) {
        client_disconnect_callback(0, reason);
    }
}
