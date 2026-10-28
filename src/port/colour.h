/*
 * Byte order of the game's packed colours.
 *
 * The original stored colours as x86 DWORDs and handed the same memory to
 * Sprite_Queue/Font_DrawText. The helpers here produce the bytes the target
 * needs without relying on host byte order: Gfx_UnpackArgbToBgra reproduces
 * the B,G,R,A bytes of an x86 0xAARRGGBB word, and OPENCMR2_RGBA32 builds a
 * word whose in-memory bytes are R,G,B,A.
 */
#ifndef OPENCMR2_PORT_COLOUR_H
#define OPENCMR2_PORT_COLOUR_H

#include "port/endian.h"
#include "port/types.h"

/*
 * Packed colour word whose in-memory bytes are R, G, B, A on every target.
 *
 * The game keeps sprite/font colours in int globals and hands a BYTE pointer
 * to Sprite_Queue/Font_DrawText, so the byte order in memory is the contract.
 * An x86 initialiser such as 0xFF4F4F4F put 4F 4F 4F FF in memory; on 68k the
 * same value puts FF 4F 4F 4F there and the sprite reads it as a red, mostly
 * transparent colour. Use this macro so the four bytes stay R, G, B, A.
 *
 * The numeric value differs between hosts by design. Read the channels from
 * the bytes; do not use shifts or masks on the packed int in portable code.
 * Requires a 32-bit int, like the original DWORD colour fields.
 */
#if OPENCMR2_ENDIAN_NEEDS_SWAP
#define OPENCMR2_RGBA32(r, g, b, a)                                                   \
    ((int)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) |      \
           (uint32_t)(a)))
#else
#define OPENCMR2_RGBA32(r, g, b, a)                                                   \
    ((int)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) |      \
           (uint32_t)(r)))
#endif

static inline void Gfx_UnpackArgbToBgra(unsigned int argb, BYTE *bgra)
{
    bgra[0] = (BYTE)(argb >> 0);
    bgra[1] = (BYTE)(argb >> 8);
    bgra[2] = (BYTE)(argb >> 16);
    bgra[3] = (BYTE)(argb >> 24);
}

#endif
