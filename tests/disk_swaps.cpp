#include "port/disk.h"
#include "port/disk_layouts.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

extern "C" void Sys_Log(const char *format, ...)
{
    (void)format;
}

static uint32_t FloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int main()
{
    {
        static const DiskRun runs[] = {{0, 0, 4}};
        static const DiskSwap swaps[] = {{0, 0, 4}};
        static const DiskLayout layout = {4, 4, runs, 1, nullptr, 0, swaps, 1};
        const uint32_t value = 0x11223344u;
        unsigned char record[4];
        uint32_t result = 0;

        Disk_Pack(record, &value, &layout);
        Disk_Unpack(&result, record, &layout);
        REQUIRE(result == value);
    }

    {
        /* The generated table for a real stage record: every scalar field
         * must survive Pack/Unpack in the forced big-endian path. */
        SceneNode node{};
        SceneNode result{};
        unsigned char record[0x18c];
        unsigned char native[sizeof(SceneNode)];

        node.translation.x = 0x12345678;
        node.translation.y = -12345;
        node.translation.z = 0x7fffffff;
        node.angles.x = 0xabcd;
        node.angles.y = 0x1234;
        node.angles.z = 0xfedc;
        node.angles.pad = 0x5678;
        node.sector = -2;
        node.slot = 7;
        node.neighbourSectors[0] = -1;
        node.neighbourSectors[1] = 0x1234;
        node.neighbourSectors[2] = 0x7fff;
        node.field_0x2e = 42;
        node.flags = 0xdeadbeefu;
        node.local.right.x = 0x01020304;
        node.local.rw = -7;
        node.current.up.y = 0x55667788;
        node.world.position.z = 0x11223344;
        node.worldF[0] = 1.5f;
        node.worldF[15] = -2.25f;
        node.cameraDistance = 0x0f0f0f0f;
        node.dirty = 3;
        node.type = 5;
        node.visible = 1;
        node.useParentWorld = 1;
        node.allocated = 1;

        Disk_Pack(record, &node, &g_diskLayout_SceneNode);
        Disk_Unpack(native, record, &g_diskLayout_SceneNode);
        memcpy(&result, native, sizeof(result));

        REQUIRE(result.translation.x == node.translation.x);
        REQUIRE(result.translation.y == node.translation.y);
        REQUIRE(result.translation.z == node.translation.z);
        REQUIRE(result.angles.x == node.angles.x);
        REQUIRE(result.angles.y == node.angles.y);
        REQUIRE(result.angles.z == node.angles.z);
        REQUIRE(result.angles.pad == node.angles.pad);
        REQUIRE(result.sector == node.sector);
        REQUIRE(result.slot == node.slot);
        REQUIRE(result.neighbourSectors[0] == node.neighbourSectors[0]);
        REQUIRE(result.neighbourSectors[1] == node.neighbourSectors[1]);
        REQUIRE(result.neighbourSectors[2] == node.neighbourSectors[2]);
        REQUIRE(result.field_0x2e == node.field_0x2e);
        REQUIRE(result.flags == node.flags);
        REQUIRE(result.local.right.x == node.local.right.x);
        REQUIRE(result.local.rw == node.local.rw);
        REQUIRE(result.current.up.y == node.current.up.y);
        REQUIRE(result.world.position.z == node.world.position.z);
        REQUIRE(FloatBits(result.worldF[0]) == FloatBits(node.worldF[0]));
        REQUIRE(FloatBits(result.worldF[15]) == FloatBits(node.worldF[15]));
        REQUIRE(result.cameraDistance == node.cameraDistance);
        REQUIRE(result.dirty == node.dirty);
        REQUIRE(result.type == node.type);
        REQUIRE(result.visible == node.visible);
        REQUIRE(result.useParentWorld == node.useParentWorld);
        REQUIRE(result.allocated == node.allocated);
    }

    return 0;
}
