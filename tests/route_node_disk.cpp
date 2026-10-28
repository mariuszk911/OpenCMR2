// .cat route nodes: little-endian positions/limits, byte flags/call ids.
#include "RallyRouteNode.h"
#include "port/endian.h"
#include "port/route_disk.h"

#include <stdio.h>
#include <string.h>

extern "C" void Sys_Log(const char *, ...) {}

static void PutLE32(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("route_node: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void FillNode(unsigned char *p, unsigned int base)
{
    int i;

    for (i = 0; i < 3; i++)
        PutLE32(p + (size_t)i * 4, base + (unsigned)i);
    for (i = 0; i < 0xc; i++)
        p[0x0c + i] = (unsigned char)(0x30 + i + base);
    p[0x18] = (unsigned char)(0x03 + base);
    p[0x19] = (unsigned char)(0xff - base);
    p[0x1a] = (unsigned char)(0x55 + base);
    p[0x1b] = (unsigned char)(0x66 + base);
    for (i = 0; i < 4; i++)
        PutLE32(p + 0x1c + (size_t)i * 4, base + 0x100 + (unsigned)i);
}

static int CheckNode(const RallyRouteNode *node, unsigned int base)
{
    int i;

    for (i = 0; i < 3; i++) {
        const unsigned char *field = (const unsigned char *)&node->position + (size_t)i * 4;
        unsigned int value;

        memcpy(&value, field, sizeof(value));
        CHECK(value == base + (unsigned)i);
    }
    for (i = 0; i < 0xc; i++)
        CHECK(node->field_0xc[i] == (unsigned char)(0x30 + i + base));
    CHECK(node->flags == (unsigned char)(0x03 + base));
    CHECK(node->callId == (char)(0xff - base));
    CHECK(node->field_0x1a[0] == (unsigned char)(0x55 + base));
    CHECK(node->field_0x1a[1] == (unsigned char)(0x66 + base));
    CHECK(node->backwardNearLimit == (int)(base + 0x100));
    CHECK(node->backwardFarLimit == (int)(base + 0x101));
    CHECK(node->forwardNearLimit == (int)(base + 0x102));
    CHECK(node->forwardFarLimit == (int)(base + 0x103));
    return 0;
}

static int LoadFile(const char *path, unsigned char *buffer, unsigned int capacity, unsigned int *outSize)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    if (f == NULL)
        return 0;
    got = fread(buffer, 1, capacity, f);
    fclose(f);
    *outSize = (unsigned int)got;
    return got > 0;
}

int main(int argc, char **argv)
{
    unsigned char file[2 * ROUTE_NODE_DISK_SIZE];
    unsigned char shifted[2 * ROUTE_NODE_DISK_SIZE + 1];
    unsigned char output[2 * sizeof(RallyRouteNode)];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;
    unsigned int count = 0;

    CHECK(sizeof(RallyRouteNode) == 0x2c);
    FillNode(file, 0x100);
    FillNode(file + ROUTE_NODE_DISK_SIZE, 0x200);
    memset(output, 0, sizeof(output));
    CHECK(RouteNodeDisk_Decode(output, sizeof(output), file, sizeof(file), &count) != 0);
    CHECK(count == 2);
    if (CheckNode((const RallyRouteNode *)output, 0x100))
        return 1;
    if (CheckNode((const RallyRouteNode *)(output + sizeof(RallyRouteNode)), 0x200))
        return 1;

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(output, 0, sizeof(output));
    CHECK(RouteNodeDisk_Decode(output, sizeof(output), shifted + 1, sizeof(file), NULL) != 0);
    if (CheckNode((const RallyRouteNode *)output, 0x100))
        return 1;

    // A partial node or a destination that is too small is rejected.
    memset(output, 0x5a, sizeof(output));
    CHECK(RouteNodeDisk_Decode(output, sizeof(output), file, sizeof(file) - 1, &count) == 0);
    CHECK(output[0] == 0x5a);
    CHECK(RouteNodeDisk_Decode(output, sizeof(output) - 1, file, sizeof(file), NULL) == 0);
    CHECK(RouteNodeDisk_Decode(NULL, sizeof(output), file, sizeof(file), NULL) == 0);
    CHECK(RouteNodeDisk_Decode(output, sizeof(output), NULL, sizeof(file), NULL) == 0);

    if (argc > 1) {
        const RallyRouteNode *nodes;

        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        CHECK(capturedSize == 256u * ROUTE_NODE_DISK_SIZE);
        // Decode into a native copy: the source is the archive member.
        {
            static RallyRouteNode nodesNative[256];
            CHECK(RouteNodeDisk_Decode(nodesNative, sizeof(nodesNative), captured, capturedSize, &count) != 0);
            CHECK(count == 256);
            nodes = nodesNative;
            CHECK(nodes[0].position.x == 8313492);
            CHECK(nodes[0].position.y == 6553);
            CHECK(nodes[0].position.z == 20906557);
            CHECK(nodes[0].flags == 1);
            CHECK(nodes[0].callId == (char)0xff);
            CHECK(nodes[0].backwardNearLimit == 9830400);
            CHECK(nodes[0].backwardFarLimit == 52428800);
            CHECK(nodes[1].forwardNearLimit == 9830400);
        }
    }

    printf("route_node: OK\n");
    return 0;
}
