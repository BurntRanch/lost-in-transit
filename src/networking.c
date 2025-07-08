#include "networking.h"
#include <stdio.h>

void NETHandleDisconnect(const enum Role role, const ConnectionHandle handle, const char * const message) {
    if (role == NET_ROLE_SERVER) {
        fprintf(stderr, "Client (%d) is disconnecting! %s\n", handle, message ? message : "");
    } else {
        fprintf(stderr, "Disconnecting from server (%d)! %s\n", handle, message ? message : "");
    }
}

void NETHandleConnect(const enum Role role, const ConnectionHandle handle) {
    if (role == NET_ROLE_SERVER) {
        fprintf(stderr, "Client (%d) has connected!\n", handle);
    } else {
        fprintf(stderr, "Connected to server (%d)!\n", handle);
    }
}

void NETHandleConnectionFailure(const char * const reason) {
    fprintf(stderr, "Failed to connect to server! (reason: %s)\n", reason ? reason : "unexpected error");
}
