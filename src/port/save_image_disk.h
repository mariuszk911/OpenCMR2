#ifndef OPENCMR2_PORT_SAVE_IMAGE_DISK_H
#define OPENCMR2_PORT_SAVE_IMAGE_DISK_H

#include <stddef.h>

// Saved-game images are little-endian on disk. The codec converts them in
// place (Unpack) or into a separate file buffer (Pack) without changing the
// game's SavedGameImage layout. Bitfields are packed by name, not by compiler
// bit position, so the m68k MSB-first allocation is handled explicitly.
bool SavedImageDisk_Unpack(void *image, size_t size);
bool SavedImageDisk_Pack(const void *image, void *bytes, size_t size);

#endif
