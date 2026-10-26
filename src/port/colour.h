/*
 * Byte order of the game's packed ARGB colours.
 *
 * The original writes a DWORD into a four-byte sprite colour, so on a
 * little-endian target the bytes are B, G, R, A. Spell that out so the 68k
 * build produces the same channels instead of relying on host byte order.
 */
#ifndef OPENCMR2_PORT_COLOUR_H
#define OPENCMR2_PORT_COLOUR_H

#include "port/types.h"

static inline void Gfx_UnpackArgbToBgra(unsigned int argb, BYTE *bgra)
{
    bgra[0] = (BYTE)(argb >> 0);
    bgra[1] = (BYTE)(argb >> 8);
    bgra[2] = (BYTE)(argb >> 16);
    bgra[3] = (BYTE)(argb >> 24);
}

#endif
