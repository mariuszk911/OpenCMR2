/*
 * .cat rally route nodes (0x2c bytes each, pointer-free).
 *
 * RallyData_LoadSelectedRouteNodes keeps a pointer into the node array and
 * later code reads the node positions, flags and call ids in place, so the
 * decoder fills a native copy owned by the archive buffer.
 */
#ifndef OPENCMR2_PORT_ROUTE_DISK_H
#define OPENCMR2_PORT_ROUTE_DISK_H

#include <stddef.h>

#define ROUTE_NODE_DISK_SIZE 0x2c

#ifdef __cplusplus
extern "C" {
#endif

/* nativeNodes must hold size/0x2c native RallyRouteNodes. Returns 1 and sets
 * *outCount on success; 0 for NULL input or a size that is not a whole number
 * of nodes. */
int RouteNodeDisk_Decode(void *nativeNodes, size_t nativeSize,
                         const void *file, size_t size,
                         unsigned int *outCount);

#ifdef __cplusplus
}
#endif

#endif
