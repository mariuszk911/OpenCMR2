#ifndef OPENCMR2_PORT_SAVE_IMAGE_DISK_H
#define OPENCMR2_PORT_SAVE_IMAGE_DISK_H

#include <stddef.h>
#include <stdint.h>

// Saved-game session images (.rcs, sizeof(SavedGameImage) == 0x7f4 bytes).
//
// Two on-disk formats exist. Both keep the game's SavedGameImage layout and
// both pack bitfields by name at explicit LSB-first positions, so the m68k
// MSB-first bitfield allocation never reaches the file.
//
//  SAVED_IMAGE_PC     the original little-endian file, byte for byte. The
//                     header[0x10] bytes are not interpreted.
//  SAVED_IMAGE_AMIGA  the Amiga-native file: every 32-bit word is big-endian
//                     and header[0x10] carries a signature, a format version
//                     and a CRC-32 of the remaining 0x7e4 bytes:
//
//                       +0x00  'C' 'M' '2' 'A'
//                       +0x04  version   (u32 BE, SAVED_IMAGE_AMIGA_VERSION)
//                       +0x08  checksum  (u32 BE, CRC-32 of bytes 0x10..0x7f3)
//                       +0x0c  flags     (u32 BE, must be zero in version 1)
//
//                     The opaque car and controller records (playerCars,
//                     playerCarSetups) are the game's in-memory bytes, which
//                     is the 68k byte order on the target the format is meant
//                     for. A PC file has no signature, so it can never be
//                     mistaken for an Amiga one.
//
// The format a build reads and writes through SavedImageDisk_Pack/Unpack is
// its native one: SAVED_IMAGE_AMIGA on a big-endian target, SAVED_IMAGE_PC
// elsewhere. The *As variants name the format explicitly (host tests).
enum SavedImageFormat {
    SAVED_IMAGE_PC,
    SAVED_IMAGE_AMIGA
};

enum SavedImageStatus {
    SAVED_IMAGE_OK = 0,
    SAVED_IMAGE_BAD_ARGUMENT,   // NULL pointer
    SAVED_IMAGE_BAD_SIZE,       // not exactly one image
    SAVED_IMAGE_FOREIGN,        // no Amiga signature: a PC file or something unrelated
    SAVED_IMAGE_UNSUPPORTED,    // Amiga signature, but a version or flags this build cannot read
    SAVED_IMAGE_CORRUPT         // Amiga image whose checksum does not match
};

#define SAVED_IMAGE_AMIGA_VERSION 1u

// CRC-32 (IEEE 802.3, the zlib polynomial) of a byte range.
uint32_t SavedImageDisk_Checksum(const void *data, size_t size);

// Converts a file image in place into the game's SavedGameImage. A rejected
// Amiga image is left untouched. PC images carry no integrity data, so the
// game's own validation is the only check for them.
SavedImageStatus SavedImageDisk_UnpackAs(SavedImageFormat format, void *image, size_t size);

// Encodes a SavedGameImage into a separate file buffer of exactly one image.
SavedImageStatus SavedImageDisk_PackAs(SavedImageFormat format, const void *image,
                                       void *bytes, size_t size);

SavedImageFormat SavedImageDisk_NativeFormat(void);

// Native-format entry points used by the game. Unpack reports every rejection
// to the handler installed with SavedImageDisk_SetRejectHandler.
bool SavedImageDisk_Unpack(void *image, size_t size);
bool SavedImageDisk_Pack(const void *image, void *bytes, size_t size);

// The platform layer learns which kind of file was refused (it shows the
// requester about PC saves); the codec itself stays free of any UI. Passing
// NULL removes the handler. Called only for FOREIGN, UNSUPPORTED and CORRUPT.
typedef void (*SavedImageRejectHandler)(SavedImageStatus status);
void SavedImageDisk_SetRejectHandler(SavedImageRejectHandler handler);

#endif
