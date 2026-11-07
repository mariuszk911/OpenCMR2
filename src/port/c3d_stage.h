/*
 * Stage C3D ("PP_F") model member: the layout of its records, the bounds that
 * keep every count inside the member and the game's registration tables, the
 * decode of its pointer-free arrays and the range check of a mesh's array
 * offsets. The header decode is port/c3d_header.h and the record unpacking is
 * port/disk.h; the registration of the records stays in game/Sector.cpp.
 *
 * Domain A (game layout): it uses the game's mesh records.
 */
#ifndef OPENCMR2_PORT_C3D_STAGE_H
#define OPENCMR2_PORT_C3D_STAGE_H

#include "port/types.h"
#include "port/c3d_header.h"

struct Mesh;
struct MeshTriangle;

/* Remaining room in the game's fixed registration tables. */
struct C3DStageLimits {
    unsigned int nodes;
    unsigned int meshes;
    unsigned int objects;
    unsigned int records;
    unsigned int sectors;
};

/* Where each array starts in the member, and the native array sizes. */
struct C3DStageLayout {
    unsigned char *nodes;
    unsigned char *meshes;      /* plain meshes, then the stage object meshes */
    unsigned char *objects;
    unsigned char *sectors;
    unsigned char *triangles;
    unsigned char *vertexData;
    unsigned char *lightLevels;
    unsigned char *vertexFlags;
    unsigned char *records;
    unsigned char *end;         /* the texture records start here */
    char *textureRecords;       /* texture-name block, from the header offset */
    unsigned long long triangleBytes;   /* native sizes, each rounded up to 8 */
    unsigned long long vertexBytes;
    unsigned long long lightBytes;
    unsigned long long flagBytes;
    unsigned long long payloadBytes;
};

#define C3D_STAGE_OK 0
#define C3D_STAGE_BAD_HEADER 1
#define C3D_STAGE_SHORT_MEMBER 2
#define C3D_STAGE_ARRAY_OVERFLOW 3
#define C3D_STAGE_CAPACITY 4
#define C3D_STAGE_EXCEEDS_MEMBER 5
#define C3D_STAGE_PAYLOAD_OVERFLOW 6

/* Decodes the header into *header and lays the records out. size is the member
 * extent (0 when unknown, which skips the extent checks). */
int C3DStage_Measure(unsigned char *data, DWORD size, const C3DStageLimits *limits,
                     C3DHeaderValues *header, C3DStageLayout *layout);

const char *C3DStage_StatusText(int status);

/* The native arrays decoded from the member. */
struct C3DStageArrays {
    struct MeshTriangle *triangles;
    unsigned char *vertexData;
    int *lightLevels;
    unsigned char *vertexFlags;
};

/* Splits payload (layout->payloadBytes bytes) into the four arrays and decodes
 * them; every triangle texture index other than -1 is moved past the
 * textureBase textures already loaded. */
void C3DStage_DecodePayload(unsigned char *payload, const C3DStageLayout *layout,
                            const C3DHeaderValues *header, int textureBase,
                            C3DStageArrays *arrays);

/* Whether the array pointers of an unpacked mesh lie on 4-byte boundaries
 * inside the decoded arrays and its triangle and vertex ranges fit. */
int C3DStage_MeshArraysValid(const struct Mesh *mesh, const C3DStageArrays *arrays,
                             const C3DHeaderValues *header);

#endif
