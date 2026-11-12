// ENet's platform headers (the Win32 SDK, or on AmigaOS the netinclude headers
// that pull in exec/types.h) conflict with port/types.h, whose game types are
// fixed width. Keep them out of translation units that use it: these are
// ENet's public socket/buffer declarations, not OS API calls. ENet itself
// still builds against the real headers in third_party/enet (on AmigaOS
// together with net/enet_bsdsocket.c); the layouts below are the same.
#ifndef OPENCMR2_ENET_PLATFORM_H
#define OPENCMR2_ENET_PLATFORM_H

#ifdef _WIN32
#include <stddef.h>
#include <stdint.h>
#define __ENET_WIN32_H__
typedef uintptr_t ENetSocket;
typedef struct fd_set ENetSocketSet; // only used behind pointers in enet.h
typedef struct {
    size_t dataLength;
    void *data;
} ENetBuffer;
#define ENET_CALLBACK __cdecl
#define ENET_API extern
#endif

#if defined(__amigaos__)
#include <stddef.h>
#define __ENET_UNIX_H__
typedef int ENetSocket;
#define ENET_SOCKET_NULL -1
typedef struct {
    void *data;
    size_t dataLength;
} ENetBuffer;
typedef struct ENetSocketSetOpaque ENetSocketSet; // only used behind pointers in enet.h
// m68k is big-endian, the network byte order: the conversions are the identity.
#define ENET_HOST_TO_NET_16(value) (value)
#define ENET_HOST_TO_NET_32(value) (value)
#define ENET_NET_TO_HOST_16(value) (value)
#define ENET_NET_TO_HOST_32(value) (value)
#define ENET_CALLBACK
#define ENET_API extern
#endif

#include <enet/enet.h>
#endif
