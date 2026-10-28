// .cat route-node decoder, see port/route_disk.h.

#include "port/route_disk.h"

#include "port/types.h"
#include "port/disk_layouts.h"

#include <stddef.h>

extern "C" int RouteNodeDisk_Decode(void *nativeNodes, size_t nativeSize,
                                    const void *file, size_t size,
                                    unsigned int *outCount)
{
    const unsigned char *src = (const unsigned char *)file;
    size_t count;
    size_t i;

    if (nativeNodes == NULL || file == NULL || size == 0 ||
            size % ROUTE_NODE_DISK_SIZE != 0)
        return 0;
    count = size / ROUTE_NODE_DISK_SIZE;
    if (nativeSize < count * sizeof(RallyRouteNode))
        return 0;
    for (i = 0; i < count; i++) {
        Disk_Unpack((unsigned char *)nativeNodes + i * sizeof(RallyRouteNode),
                    src + i * ROUTE_NODE_DISK_SIZE, &g_diskLayout_RallyRouteNode);
    }
    if (outCount != NULL)
        *outCount = (unsigned int)count;
    return 1;
}
