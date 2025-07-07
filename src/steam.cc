#include "steam.hh"
#include "steam/isteamnetworkingsockets.h"

#include <cstddef>
#include <stdio.h>
#include <stdlib.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

static HSteamListenSocket server_listen_socket = k_HSteamListenSocket_Invalid;
static HSteamNetPollGroup server_poll_group = k_HSteamNetPollGroup_Invalid;

static ISteamNetworkingSockets *steam_networking_interface;

/* TODO: implement */
// static void Server_NetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo) {
//     /* do something */
// }

bool SRInitGNS(void) {
    SteamDatagramErrMsg errMsg;

    if (!GameNetworkingSockets_Init(NULL, errMsg)) {
        fprintf(stderr, "Failed to initialize GameNetworkingSockets! (errMsg: %s)\n", errMsg);
        return false;
    }

    return true;
}

char *SRStartServer(Uint16 port) {
    if (server_listen_socket != k_HSteamListenSocket_Invalid) {
        return NULL;
    }

    steam_networking_interface = SteamNetworkingSockets();

    SteamNetworkingIPAddr ipAddr;
    ipAddr.Clear();
    ipAddr.ParseString("127.0.0.1");
    ipAddr.m_port = port;

    server_listen_socket = steam_networking_interface->CreateListenSocketIP(ipAddr, 1, NULL);
    if (server_listen_socket == k_HSteamListenSocket_Invalid) {
        fprintf(stderr, "Failed to create socket on port %d.\n", port);
        return NULL;
    }

    server_poll_group = steam_networking_interface->CreatePollGroup();
    if (server_poll_group == k_HSteamNetPollGroup_Invalid) {
        fprintf(stderr, "Failed to create server poll group.");

        SRStopServer();
        return NULL;
    }

    char *ip_addr_str = (char *)malloc(128);
    ipAddr.ToString(ip_addr_str, 128, true);

    return ip_addr_str;
}

void SRStopServer(void) {
    steam_networking_interface->DestroyPollGroup(server_poll_group);
    server_poll_group = k_HSteamNetPollGroup_Invalid;

    steam_networking_interface->CloseListenSocket(server_listen_socket);
    server_listen_socket = k_HSteamListenSocket_Invalid;
}
