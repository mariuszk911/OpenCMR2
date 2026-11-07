// Stage collision block decoder, see port/track_collision_disk.h.

#include "port/track_collision_disk.h"

#include "port/types.h"
#include "port/c3d_payload.h"
#include "port/endian.h"

#include <stdint.h>

namespace {

const unsigned int kHeaderSize = 0x14;

struct Counts {
    int levels[5];
    unsigned int vertices;
    unsigned int triangles;
    unsigned int list;
    uint64_t nodes;
    uint64_t totalBytes;
};

// Validates every count against size and returns 1 with the table sizes.
int Measure(const unsigned char *file, unsigned int size, Counts *counts)
{
    const unsigned char *pFile = file;
    int i;

    if (file == NULL || size < kHeaderSize)
        return 0;
    counts->nodes = 0;
    for (i = 0; i < 5; i++) {
        counts->levels[i] = (short)ReadLE16(pFile + offsetof(TrackCollisionHeader, levelCounts) + i * 2);
        if (counts->levels[i] < 0)
            return 0;
        counts->nodes += (unsigned int)counts->levels[i];
    }
    pFile += kHeaderSize;
    if ((uint64_t)(pFile - file) + counts->nodes * sizeof(TrackQuadNode) + sizeof(TrackCollisionCount) > size)
        return 0;
    pFile += counts->nodes * sizeof(TrackQuadNode);
    counts->vertices = ReadLE16(pFile);
    pFile += sizeof(TrackCollisionCount);
    if ((uint64_t)(pFile - file) + (uint64_t)counts->vertices * sizeof(FixVector) + sizeof(TrackCollisionCount) > size)
        return 0;
    pFile += counts->vertices * sizeof(FixVector);
    counts->triangles = ReadLE16(pFile);
    pFile += sizeof(TrackCollisionCount);
    if ((uint64_t)(pFile - file) + (uint64_t)counts->triangles * sizeof(TrackTriangle) + sizeof(int) > size)
        return 0;
    pFile += counts->triangles * sizeof(TrackTriangle);
    counts->list = ReadLE32(pFile);
    pFile += sizeof(int);
    if ((uint64_t)(pFile - file) + (uint64_t)counts->list * sizeof(short) + 2 * sizeof(short) > size)
        return 0;

    counts->totalBytes = kHeaderSize + counts->nodes * sizeof(TrackQuadNode) +
                         sizeof(TrackCollisionCount) + counts->vertices * sizeof(FixVector) +
                         sizeof(TrackCollisionCount) + counts->triangles * sizeof(TrackTriangle) +
                         sizeof(int) + counts->list * sizeof(short) + 2 * sizeof(short);
    if (counts->totalBytes > (uint64_t)size || counts->totalBytes > (uint64_t)(size_t)-1)
        return 0;
    return 1;
}

} // namespace

size_t TrackCollisionDisk_NativeSize(const unsigned char *file, unsigned int size)
{
    Counts counts;

    if (!Measure(file, size, &counts))
        return 0;
    return (size_t)counts.totalBytes;
}

int TrackCollisionDisk_Decode(void *native, size_t nativeSize,
                              const unsigned char *file, unsigned int size,
                              TrackCollisionViews *views)
{
    Counts counts;
    unsigned char *out;
    const unsigned char *pFile;
    TrackCollisionCount *pCount;
    int i;

    if (native == NULL || views == NULL || !Measure(file, size, &counts) ||
            nativeSize < counts.totalBytes)
        return 0;

    out = (unsigned char *)native;
    views->header = (TrackCollisionHeader *)out;
    views->header->x = (int)ReadLE32(file + 0x00);
    views->header->z = (int)ReadLE32(file + 0x04);
    for (i = 0; i < 5; i++) {
        views->header->levelCounts[i] = (short)counts.levels[i];
        views->levelCounts[i] = counts.levels[i];
    }
    views->header->field_0x12 = (short)ReadLE16(file + 0x12);
    out += kHeaderSize;
    pFile = file + kHeaderSize;
    for (i = 0; i < 5; i++) {
        TrackQuadNode *pNodes = (TrackQuadNode *)out;
        views->levels[i] = pNodes;
        for (int j = 0; j < counts.levels[i]; j++, pFile += 8, out += 8) {
            pNodes[j].triangleCount = (short)ReadLE16(pFile + 0);
            pNodes[j].field_0x2 = (short)ReadLE16(pFile + 2);
            pNodes[j].firstIndex = (int)ReadLE32(pFile + 4);
        }
    }
    pCount = (TrackCollisionCount *)out;
    views->vertexCount = pCount;
    pCount->count = ReadLE16(pFile + 0);
    pCount->field_0x2 = ReadLE16(pFile + 2);
    pFile += sizeof(TrackCollisionCount);
    out += sizeof(TrackCollisionCount);
    views->vertices = (FixVector *)out;
    C3D_DecodeWords(views->vertices, pFile, counts.vertices * sizeof(FixVector));
    pFile += counts.vertices * sizeof(FixVector);
    out += counts.vertices * sizeof(FixVector);
    pCount = (TrackCollisionCount *)out;
    views->triangleCount = pCount;
    pCount->count = ReadLE16(pFile + 0);
    pCount->field_0x2 = ReadLE16(pFile + 2);
    pFile += sizeof(TrackCollisionCount);
    out += sizeof(TrackCollisionCount);
    views->triangles = (TrackTriangle *)out;
    for (i = 0; i < (int)counts.triangles; i++, pFile += 8, out += 8) {
        TrackTriangle *pTriangle = &views->triangles[i];
        unsigned short bits = ReadLE16(pFile + 6);
        pTriangle->v[0] = ReadLE16(pFile + 0);
        pTriangle->v[1] = ReadLE16(pFile + 2);
        pTriangle->v[2] = ReadLE16(pFile + 4);
        pTriangle->surface = (unsigned short)(bits & 0x7f);
        pTriangle->flags = (unsigned short)(bits >> 7);
    }
    views->triangleListCount = (int *)out;
    *views->triangleListCount = (int)counts.list;
    pFile += sizeof(int);
    out += sizeof(int);
    views->triangleIndices = (short *)out;
    for (i = 0; i < (int)counts.list; i++, pFile += 2, out += 2)
        views->triangleIndices[i] = (short)ReadLE16(pFile);
    views->gridColumns = (short *)out;
    *views->gridColumns = (short)ReadLE16(pFile);
    pFile += 2;
    out += 2;
    views->gridRows = (short *)out;
    *views->gridRows = (short)ReadLE16(pFile);
    return 1;
}
