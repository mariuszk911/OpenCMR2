/*
 * The .pcf font format: header, 256-entry character map, 0x10-byte glyph
 * records and kerning pairs, all little-endian on disk.
 *
 * The file buffer is often part of the read-only archive, so the parser
 * builds native copies instead of relocating the records in place. This is
 * fork-owned: game/Font.cpp only binds the result to a font slot.
 *
 * Domain A (game layout): it uses the game's font structures, so it must be
 * compiled with the game's alignment flags and never included from the
 * OS/renderer glue.
 */
#ifndef OPENCMR2_PORT_FONT_PCF_H
#define OPENCMR2_PORT_FONT_PCF_H

#include "port/types.h"

struct FontSlot;
struct GenericFile;

/* Bytes readable from data to the end of the archive buffer, or 0 when the
 * data is not inside an archive (the extent is unknown). */
DWORD FontPcf_ArchiveExtent(struct GenericFile *file, const void *data, int inArchive);

/* Builds the native header/map/glyph/kerning tables for slot and points the
 * slot at them. A previous load for index is released first. Returns 1 on
 * success; the caller owns the file buffer and sets slot->loaded. */
int FontPcf_Setup(struct FontSlot *slot, unsigned int index, const void *data, DWORD dataSize);

/* Frees the native tables and points the slot at its idle zeroed header/map,
 * so an unloaded slot is safe to read. Does not touch slot->pData. */
void FontPcf_Release(struct FontSlot *slot, unsigned int index);

#endif
