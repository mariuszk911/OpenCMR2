/*
 * Texture file and C3D texture-record decoders: the .dds header words and
 * the C3D texture-record table. All values are little-endian
 * on disk and the data may be unaligned inside an archive member, so every
 * field is read through ReadLE* at its file offset.
 *
 * Domain A (game layout): DdsDisk_Decode uses the game's DDSHeader.
 */
#ifndef OPENCMR2_PORT_TEXTURE_DISK_H
#define OPENCMR2_PORT_TEXTURE_DISK_H

#include <stddef.h>

struct DDSHeader;

#ifdef __cplusplus
extern "C" {
#endif

/* Converts the header words the loader reads (flags, size, linear size,
 * FourCC, bit count, alpha mask) of a header copied from the file, in place. */
void DdsDisk_Decode(struct DDSHeader *header);

/* C3D texture records: a 0x10-byte header whose 16-bit count at +0xc is
 * followed by that many 8-byte slots; the 16-bit name index of a slot is at
 * +0x4 of the slot. Names are 0x104-byte slots of the texture-name block. */
#define TEXTURE_RECORD_OK 0
#define TEXTURE_RECORD_OVERRUN 1      /* header does not fit before limit */
#define TEXTURE_RECORD_BAD_COUNT 2    /* count too large or slots overrun limit */
#define TEXTURE_RECORD_BAD_INDEX 3    /* name index outside the name block */
#define TEXTURE_RECORD_UNTERMINATED 4 /* name slot has no terminator */

/* Reads the slot count of the record at rec; limit may be NULL (unbounded).
 * *count is set whenever the header could be read. */
int TextureRecord_Open(const unsigned char *rec, const unsigned char *limit,
                       unsigned int *count);

/* The record that follows rec, which holds count slots. */
const unsigned char *TextureRecord_Next(const unsigned char *rec, unsigned int count);

/* Name of slot i of rec in a block of nameCount names. *index receives the
 * slot's name index. On any status other than OK *name is NULL. */
int TextureRecord_Name(const unsigned char *rec, unsigned int i, char *names,
                       unsigned int nameCount, char **name, unsigned int *index);

const char *TextureRecord_StatusText(int status);

#ifdef __cplusplus
}
#endif

#endif
