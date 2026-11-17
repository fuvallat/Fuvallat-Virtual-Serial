#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
    typedef enum {
        NETPROTO_TCP,
        NETPROTO_UDP
    } NET_PROTOCOL;

    HANDLE OpenNetworkConnection(PCWSTR host, USHORT port, NET_PROTOCOL proto);

    static inline HANDLE OpenNetworkConnectionDefault(PCWSTR host, USHORT port) {
        return OpenNetworkConnection(host, port, NETPROTO_TCP);
    }
#ifdef __cplusplus
}
#endif