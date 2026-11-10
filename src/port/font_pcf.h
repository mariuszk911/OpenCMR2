/*
 * The .pcf font format: a 0x18-byte header, a 256-entry character map,
 * 0x10-byte glyph records and 4-byte kerning pairs, all little-endian on disk.
 *
 * FontPcf_Decode turns a member into the native form upstream's Font_Setup
 * reads (same size, every field in host order), after checking that the
 * counts and offsets stay inside the member. It is registered in
 * port/member_decode.cpp, so game/Font.cpp is upstream's code.
 */
#ifndef OPENCMR2_PORT_FONT_PCF_H
#define OPENCMR2_PORT_FONT_PCF_H

#include "port/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* native: at least size bytes. Returns 1 on success, 0 for a NULL input or a
 * member that is too short or has no, too many or truncated glyph records, or
 * too many kerning pairs in all. Character map entries outside the glyph table
 * become -1. A glyph whose kerning run is invalid (too long, misplaced or
 * outside the member, as the unused last record of the 1024 .pcf files is)
 * keeps its glyph data and loses its kerning. */
int FontPcf_Decode(void *native, size_t nativeSize, const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
