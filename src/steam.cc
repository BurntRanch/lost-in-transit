#include "steam.hh"
#include "networking.h"
#include "steam/isteamnetworkingsockets.h"
#include "steam/steamclientpublic.h"

#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <assert.h>
#include <cstddef>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <vector>

static HSteamListenSocket server_listen_socket = k_HSteamListenSocket_Invalid;
static HSteamNetPollGroup server_poll_group = k_HSteamNetPollGroup_Invalid;
static SteamNetworkingConfigValue_t server_config;

/* please forgive me yall for any stupid thing i do because working with C++ is like working with a picky eater child. */
static std::vector<HSteamNetConnection> server_clients;

static HSteamNetConnection client_connection = k_HSteamNetConnection_Invalid;
static SteamNetworkingConfigValue_t client_config;

static void Server_NetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo) {
    switch (pInfo->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_None:
            break;  /* uh okay */
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            /* connections that were never accepted don't matter to us */
            if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected) {
                NETHandleDisconnect(NET_ROLE_SERVER, pInfo->m_hConn, NULL);

                auto it = std::find(server_clients.begin(), server_clients.end(), pInfo->m_hConn);
                if (it != server_clients.end()) {
                    server_clients.erase(it);
                }
            }

            SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, NULL, false);
            break;
        case k_ESteamNetworkingConnectionState_Connecting:
            /* if this leads to some DoS attack where people can "pretend to connect twice" please do not talk to me */
            assert(std::find(server_clients.begin(), server_clients.end(), pInfo->m_hConn) == server_clients.end());

            if (SteamNetworkingSockets()->AcceptConnection(pInfo->m_hConn) != k_EResultOK) {
                SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, NULL, false);
                fprintf(stderr, "Failed to accept client connection!\n");
                break;
            }

            if (!SteamNetworkingSockets()->SetConnectionPollGroup(pInfo->m_hConn, server_poll_group)) {
                SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, NULL, false);
                fprintf(stderr, "Failed to assign poll group to client connection!\n");
                break;
            }

            NETHandleConnect(NET_ROLE_SERVER, pInfo->m_hConn);
            server_clients.push_back(pInfo->m_hConn);

            break;
        default:
            break;
    }
}

static void Client_NetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo) {
    switch (pInfo->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_None:
            break;  /* uh okay */
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting) {
                /* we were never connected, either we were refused access or something else went wrong. */
                NETHandleConnectionFailure(pInfo->m_info.m_szEndDebug);
            } else {
                NETHandleDisconnect(NET_ROLE_CLIENT, pInfo->m_hConn, pInfo->m_info.m_szEndDebug);
            }

            SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, NULL, false);
            break;
        case k_ESteamNetworkingConnectionState_Connected:
            /* look mom! I got accepted! */
            NETHandleConnect(NET_ROLE_CLIENT, pInfo->m_hConn);
            break;
        default:
            break;
    }
}

bool SRInitGNS(void) {
    SteamDatagramErrMsg errMsg;

    if (!GameNetworkingSockets_Init(NULL, errMsg)) {
        fprintf(stderr, "Failed to initialize GameNetworkingSockets! (errMsg: %s)\n", errMsg);
        return false;
    }

    server_config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void *)Server_NetConnectionStatusChanged);
    client_config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void *)Client_NetConnectionStatusChanged);

    return true;
}

char *SRStartServer(Uint16 port) {
    if (server_listen_socket != k_HSteamListenSocket_Invalid) {
        return NULL;
    }

    SteamNetworkingIPAddr ipAddr;
    ipAddr.Clear();
    ipAddr.ParseString("127.0.0.1");
    ipAddr.m_port = port;

    server_listen_socket = SteamNetworkingSockets()->CreateListenSocketIP(ipAddr, 1, &server_config);
    if (server_listen_socket == k_HSteamListenSocket_Invalid) {
        fprintf(stderr, "Failed to create socket on port %d.\n", port);
        return NULL;
    }

    server_poll_group = SteamNetworkingSockets()->CreatePollGroup();
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
    for (const HSteamNetConnection &conn : server_clients) {
        SteamNetworkingSockets()->CloseConnection(conn, 0, "Server shutting down", true);
        server_clients.erase(server_clients.begin());
    }

    SteamNetworkingSockets()->DestroyPollGroup(server_poll_group);
    server_poll_group = k_HSteamNetPollGroup_Invalid;

    SteamNetworkingSockets()->CloseListenSocket(server_listen_socket);
    server_listen_socket = k_HSteamListenSocket_Invalid;
}

static bool ConnectToServer(const SteamNetworkingIPAddr * const pAddress) {
    if (client_connection != k_HSteamNetConnection_Invalid) {
        fprintf(stderr, "Already connected to a server!\n");
        return false;
    }

    if ((client_connection = SteamNetworkingSockets()->ConnectByIPAddress(*pAddress, 1, &client_config) == k_HSteamNetConnection_Invalid)) {
        fprintf(stderr, "Failed to connect to server!\n");
        return false;
    }

    return true;
}

bool SRConnectToServerIPv4(Uint32 ipv4, Uint16 port) {
    SteamNetworkingIPAddr ipAddr;
    ipAddr.Clear();
    ipAddr.SetIPv4(ipv4, port);

    return ConnectToServer(&ipAddr);
}

bool SRConnectToServerIPv6(Uint8 *pIPv6, Uint16 port) {
    SteamNetworkingIPAddr ipAddr;
    ipAddr.Clear();
    ipAddr.SetIPv6(pIPv6, port);

    return ConnectToServer(&ipAddr);
}

void SRDisconnectFromServer(void) {
    if (client_connection) {
        /* TODO: send goodbye message */
        SteamNetworkingSockets()->CloseConnection(client_connection, 0, "Disconnecting", true);
        client_connection = k_HSteamNetConnection_Invalid;
    }
}

bool SRPollConnections(void) {
    if (server_poll_group != k_HSteamNetPollGroup_Invalid) {
        SteamNetworkingMessage_t *messages = nullptr;
        int msgCount = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(server_poll_group, &messages, 5);

        if (msgCount < -1) {
            fprintf(stderr, "Failed to receive messages from poll group!\n");
            return false;
        }

        for (int i = 0; i < msgCount; i++) {
            SteamNetworkingMessage_t *message = &messages[i];

            /* TODO: do something */

            message->Release();
        }
    }

    SteamNetworkingSockets()->RunCallbacks();

    return true;
}
