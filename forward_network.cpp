#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <strsafe.h>
#pragma comment(lib, "ws2_32.lib")

#include "forward_network.h"

HANDLE OpenNetworkConnection(PCWSTR host, USHORT port, NET_PROTOCOL proto)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int socktype = (proto == NETPROTO_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (proto == NETPROTO_TCP) ? IPPROTO_TCP : IPPROTO_UDP;

    SOCKET s = socket(AF_UNSPEC, socktype, protocol);
    if (s == INVALID_SOCKET)
        return INVALID_HANDLE_VALUE;

    struct addrinfoW hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;

    WCHAR portBuf[16];
    StringCchPrintfW(portBuf, 16, L"%hu", port);

    struct addrinfoW* result = NULL;
    if (GetAddrInfoW(host, portBuf, &hints, &result) != 0) {
        closesocket(s);
        return INVALID_HANDLE_VALUE;
    }

    if (connect(s, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        FreeAddrInfoW(result);
        closesocket(s);
        return INVALID_HANDLE_VALUE;
    }

    FreeAddrInfoW(result);

    int timeout = 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    int sndbuf = 131072;
    int rcvbuf = 131072;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    if (proto == NETPROTO_TCP) {
        int nodelay = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        int keepalive = 1;
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepalive, sizeof(keepalive));
    }
    else if (proto == NETPROTO_UDP) {
        int broadcast = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));
    }

    return (HANDLE)s;
}