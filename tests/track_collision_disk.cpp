// Stage collision block decoder (src/port/track_collision_disk.cpp). The
// fixture is written through WriteLE*, so the forced big-endian variant
// proves the swap path on the little-endian host.
#include "port/types.h"

#include "TrackCollisionData.h"
#include "port/endian.h"
#include "port/track_collision_disk.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int Fail(const char *what, int line)
{
    std::printf("track_collision_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

const int kLevels[5] = {1, 2, 0, 0, 1}; // 4 nodes
const unsigned int kVertices = 2;
const unsigned int kTriangles = 2;
const unsigned int kList = 3;

void Put16(std::vector<unsigned char> &b, size_t at, unsigned int v)
{
    WriteLE16(&b[at], (uint16_t)v);
}

void Put32(std::vector<unsigned char> &b, size_t at, unsigned int v)
{
    WriteLE32(&b[at], (uint32_t)v);
}

// header 0x14, 4 nodes * 8, count 4, 2 vertices * 12, count 4,
// 2 triangles * 8, list count 4, 3 indices * 2, columns 2, rows 2.
std::vector<unsigned char> MakeBlock()
{
    size_t at = 0;
    int i;
    std::vector<unsigned char> b(0x14 + 4 * 8 + 4 + 2 * 12 + 4 + 2 * 8 + 4 + 3 * 2 + 2 + 2, 0xee);

    Put32(b, 0, 0x11223344);
    Put32(b, 4, 0xfffff000u);
    for (i = 0; i < 5; i++)
        Put16(b, 8 + i * 2, (unsigned)kLevels[i]);
    Put16(b, 0x12, 0x7777);
    at = 0x14;
    for (i = 0; i < 4; i++, at += 8) {
        Put16(b, at, i == 1 ? 0xffff : (unsigned)(10 + i)); // -1 internal node
        Put16(b, at + 2, (unsigned)(20 + i));
        Put32(b, at + 4, 0x30000 + (unsigned)i);
    }
    Put16(b, at, kVertices);
    Put16(b, at + 2, 0x55);
    at += 4;
    for (i = 0; i < 6; i++, at += 4)
        Put32(b, at, 0x80000000u + (unsigned)i);
    Put16(b, at, kTriangles);
    Put16(b, at + 2, 0x66);
    at += 4;
    for (i = 0; i < 2; i++, at += 8) {
        Put16(b, at + 0, 100 + (unsigned)i);
        Put16(b, at + 2, 200 + (unsigned)i);
        Put16(b, at + 4, 0xfff0 + (unsigned)i);
        Put16(b, at + 6, (unsigned)((0x1ff - i) << 7 | (0x41 + i))); // flags 9 bits, surface 7 bits
    }
    Put32(b, at, kList);
    at += 4;
    for (i = 0; i < 3; i++, at += 2)
        Put16(b, at, 0xfffe + (unsigned)i);
    Put16(b, at, 7);
    Put16(b, at + 2, 9);
    return b;
}

int CheckViews(const TrackCollisionViews &v)
{
    int i;

    CHECK(v.header->x == 0x11223344 && v.header->z == (int)0xfffff000u);
    CHECK(v.header->field_0x12 == 0x7777);
    for (i = 0; i < 5; i++) {
        CHECK(v.levelCounts[i] == kLevels[i] && v.header->levelCounts[i] == kLevels[i]);
    }
    CHECK(v.levels[0] == (TrackQuadNode *)((unsigned char *)v.header + 0x14));
    CHECK(v.levels[1] == v.levels[0] + 1 && v.levels[2] == v.levels[1] + 2);
    CHECK(v.levels[0][0].triangleCount == 10 && v.levels[0][0].field_0x2 == 20 &&
          v.levels[0][0].firstIndex == 0x30000);
    CHECK(v.levels[1][0].triangleCount == -1 && v.levels[1][0].firstIndex == 0x30001);
    CHECK(v.levels[4][0].triangleCount == 13 && v.levels[4][0].firstIndex == 0x30003);
    CHECK(v.vertexCount->count == kVertices && v.vertexCount->field_0x2 == 0x55);
    CHECK(v.vertices == (FixVector *)(v.vertexCount + 1));
    CHECK(std::memcmp(&v.vertices[0], &v.vertices[0], sizeof(FixVector)) == 0);
    {
        unsigned int word[6];
        std::memcpy(word, v.vertices, sizeof(word));
        for (i = 0; i < 6; i++)
            CHECK(word[i] == 0x80000000u + (unsigned)i);
    }
    CHECK(v.triangleCount->count == kTriangles && v.triangleCount->field_0x2 == 0x66);
    CHECK(v.triangles == (TrackTriangle *)(v.triangleCount + 1));
    CHECK(v.triangles[0].v[0] == 100 && v.triangles[0].v[1] == 200 && v.triangles[0].v[2] == 0xfff0);
    CHECK(v.triangles[0].surface == 0x41 && v.triangles[0].flags == 0x1ff);
    CHECK(v.triangles[1].v[2] == 0xfff1 && v.triangles[1].surface == 0x42 && v.triangles[1].flags == 0x1fe);
    CHECK(*v.triangleListCount == (int)kList);
    CHECK(v.triangleIndices == (short *)(v.triangleListCount + 1));
    CHECK(v.triangleIndices[0] == (short)0xfffe && v.triangleIndices[2] == (short)0x0000);
    CHECK(*v.gridColumns == 7 && *v.gridRows == 9);
    return 0;
}

int TestDecode()
{
    std::vector<unsigned char> block = MakeBlock();
    // An odd offset proves the file bytes may be unaligned.
    std::vector<unsigned char> shifted(block.size() + 1);
    std::memcpy(&shifted[1], block.data(), block.size());
    const unsigned char *file = &shifted[1];
    unsigned int size = (unsigned)block.size();
    size_t need = TrackCollisionDisk_NativeSize(file, size);
    TrackCollisionViews views;
    std::vector<unsigned long long> native; // 8-byte aligned storage

    CHECK(need == block.size());
    native.resize((need + 7) / 8);
    std::memset(&views, 0, sizeof(views));
    CHECK(TrackCollisionDisk_Decode(native.data(), need, file, size, &views) == 1);
    CHECK(views.gridRows == (short *)((unsigned char *)native.data() + need - 2));
    if (CheckViews(views) != 0)
        return 1;

    // The native copy needs the full size.
    std::memset(&views, 0, sizeof(views));
    CHECK(TrackCollisionDisk_Decode(native.data(), need - 1, file, size, &views) == 0);
    CHECK(views.header == NULL);
    CHECK(TrackCollisionDisk_Decode(NULL, need, file, size, &views) == 0);
    CHECK(TrackCollisionDisk_Decode(native.data(), need, file, size, NULL) == 0);
    return 0;
}

int TestRejects()
{
    std::vector<unsigned char> block = MakeBlock();
    unsigned int size = (unsigned)block.size();
    unsigned int cut;

    CHECK(TrackCollisionDisk_NativeSize(NULL, size) == 0);
    CHECK(TrackCollisionDisk_NativeSize(block.data(), 0x13) == 0);
    // Every truncation of a valid block is rejected; a longer member is fine.
    for (cut = 0; cut < size; cut++)
        CHECK(TrackCollisionDisk_NativeSize(block.data(), cut) == 0);
    CHECK(TrackCollisionDisk_NativeSize(block.data(), size + 100) == size);

    // A negative level count.
    Put16(block, 8 + 3 * 2, 0xffff);
    CHECK(TrackCollisionDisk_NativeSize(block.data(), size) == 0);
    block = MakeBlock();
    // Huge counts must not overflow the size checks.
    Put16(block, 8, 0x7fff);
    Put16(block, 10, 0x7fff);
    Put16(block, 12, 0x7fff);
    CHECK(TrackCollisionDisk_NativeSize(block.data(), size) == 0);
    block = MakeBlock();
    Put32(block, 0x14 + 4 * 8 + 4 + 2 * 12 + 4 + 2 * 8, 0xffffffffu); // list count
    CHECK(TrackCollisionDisk_NativeSize(block.data(), size) == 0);
    return 0;
}

} // namespace

int main()
{
    int failed = TestDecode();

    if (failed == 0)
        failed = TestRejects();
    if (failed == 0)
        std::printf("track_collision_disk: OK\n");
    return failed;
}
