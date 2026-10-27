#ifndef OPENCMR2_PORT_C3D_HEADER_H
#define OPENCMR2_PORT_C3D_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "port/endian.h"

#define C3D_HEADER_SIZE 0x30u

/*
 * Little-endian C3D ("PP_F") stage-model header. The game struct of the same
 * shape lives in game/Sector.cpp; its offsets are asserted there against these
 * reads. Decode from bytes instead of casting the file buffer: the header can
 * be unaligned inside a BFL member and is never host-endian.
 */
typedef struct C3DHeaderValues {
    unsigned int field0x0;
    unsigned int textureFile;
    unsigned int field0x8;
    unsigned int textureNamesOffset;
    unsigned int vertexFlagCount;
    unsigned int vertexCount;
    unsigned int nodeCount;
    unsigned int meshCount;
    unsigned int objectCount;
    unsigned int sectorCount;
    unsigned int triangleCount;
    unsigned int textureCount;
    unsigned int recordCount;
    unsigned int partCount;
    unsigned int field0x2c;
} C3DHeaderValues;

static inline int C3DHeader_Decode(const void *data, C3DHeaderValues *out)
{
    const unsigned char *p = (const unsigned char *)data;

    if (p == NULL || out == NULL)
        return 0;
    out->field0x0 = ReadLE32(p + 0x00);
    out->textureFile = ReadLE32(p + 0x04);
    out->field0x8 = ReadLE32(p + 0x08);
    out->textureNamesOffset = ReadLE32(p + 0x0c);
    out->vertexFlagCount = ReadLE32(p + 0x10);
    out->vertexCount = ReadLE32(p + 0x14);
    out->nodeCount = ReadLE16(p + 0x18);
    out->meshCount = ReadLE16(p + 0x1a);
    out->objectCount = ReadLE16(p + 0x1c);
    out->sectorCount = ReadLE16(p + 0x1e);
    out->triangleCount = ReadLE32(p + 0x20);
    out->textureCount = ReadLE16(p + 0x24);
    out->recordCount = ReadLE16(p + 0x26);
    out->partCount = ReadLE16(p + 0x28);
    out->field0x2c = ReadLE32(p + 0x2c);
    return 1;
}

#endif
