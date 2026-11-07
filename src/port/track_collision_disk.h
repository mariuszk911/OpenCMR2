/*
 * Serialized stage collision block (the .hpc member): a 0x14-byte header, the
 * quadtree nodes of five levels, the track vertices, the triangles, the
 * triangle index list and the grid size, all little-endian on disk.
 *
 * The block is a read-only archive member with no self-describing length, so
 * every count is validated against the member size and the tables are decoded
 * into an aligned native copy owned by the caller's buffer.
 *
 * Domain A (game layout): it uses the collision records of
 * game/TrackCollisionData.h.
 */
#ifndef OPENCMR2_PORT_TRACK_COLLISION_DISK_H
#define OPENCMR2_PORT_TRACK_COLLISION_DISK_H

#include "port/types.h"
#include "TrackCollisionData.h"

#include <stddef.h>

/* Pointers into the native copy, one per table. */
struct TrackCollisionViews {
    TrackCollisionHeader *header;
    TrackQuadNode *levels[5];
    int levelCounts[5];
    TrackCollisionCount *vertexCount;
    FixVector *vertices;
    TrackCollisionCount *triangleCount;
    TrackTriangle *triangles;
    int *triangleListCount;
    short *triangleIndices;
    short *gridColumns;
    short *gridRows;
};

/* Bytes the native copy of the block needs, or 0 when the block is rejected
 * (shorter than the header, a negative level count, or any count that does
 * not fit size). */
size_t TrackCollisionDisk_NativeSize(const unsigned char *file, unsigned int size);

/* Decodes the block into native (at least TrackCollisionDisk_NativeSize bytes,
 * suitably aligned) and fills views. Returns 1 on success; on failure nothing
 * is written. */
int TrackCollisionDisk_Decode(void *native, size_t nativeSize,
                              const unsigned char *file, unsigned int size,
                              TrackCollisionViews *views);

#endif
