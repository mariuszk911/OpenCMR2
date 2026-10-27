#ifndef OPENCMR2_PORT_C3D_PAYLOAD_H
#define OPENCMR2_PORT_C3D_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "port/endian.h"

#define C3D_TRIANGLE_SIZE 0x4cu

static inline void C3D_StoreHost16(void *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static inline void C3D_StoreHost32(void *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

/*
 * MeshTriangle as stored in the C3D file: 0x4c bytes, little-endian scalars.
 * The caller uses a native, aligned structure for the destination; this copies
 * the byte fields and converts the scalar offsets the game reads.
 */
static inline void C3D_DecodeTriangle(void *destination, const void *source)
{
    const unsigned char *src = (const unsigned char *)source;
    unsigned char *dst = (unsigned char *)destination;
    unsigned int k;

    memcpy(dst, src, C3D_TRIANGLE_SIZE);
    C3D_StoreHost16(dst + 0x00, ReadLE16(src + 0x00));            // flags
    for (k = 0; k < 10; k++)
        C3D_StoreHost32(dst + 0x04 + k * 4, ReadLE32(src + 0x04 + k * 4)); // textures
    C3D_StoreHost32(dst + 0x2c, ReadLE32(src + 0x2c));            // field_0x2c
    C3D_StoreHost32(dst + 0x30, ReadLE32(src + 0x30));            // field_0x30
    for (k = 0; k < 3; k++)
        C3D_StoreHost16(dst + 0x40 + k * 2, ReadLE16(src + 0x40 + k * 2)); // vertexIndex
}

/* Converts a pointer-free array of 32-bit scalars (floats included, bitwise). */
static inline void C3D_DecodeWords(void *destination, const void *source, size_t bytes)
{
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    size_t i;

    if ((bytes & 3u) != 0)
        return;
    for (i = 0; i < bytes; i += 4)
        C3D_StoreHost32(dst + i, ReadLE32(src + i));
}

#endif
