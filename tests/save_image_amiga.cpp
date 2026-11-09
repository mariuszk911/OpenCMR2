// The Amiga-native saved-game image: big-endian words, a signature, a version
// and a checksum in header[0x10]. The explicit-format API runs on every host;
// the build with OPENCMR2_TEST_FORCE_AMIGA_SAVES also exercises the native
// entry points and the reject handler the way the Amiga build uses them.
#include "SavedGame.h"
#include "port/save_image_disk.h"

#include <cstdio>
#include <cstring>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

unsigned long RawBE32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

void Fill(SavedGameImage &image)
{
    memset(&image, 0, sizeof(image));
    strcpy(image.name, "Amiga Rally");

    image.options.mode = 0x2a;
    image.options.difficulty = 2;
    image.options.playerCount = 3;
    image.options.multiplayer = 1;
    image.options.reserved = 0x12345;

    for (int i = 0; i < 16; ++i) {
        image.drivers[i].car = (unsigned)(i + 1);
        image.drivers[i].entryFlag = (unsigned)(i & 1);
        image.drivers[i].seconds = (unsigned)(0x40 + i);
        image.drivers[i].reserved = (unsigned)(0x1000 + i);
        image.drivers[i].time = 0x01020304 + i;
        image.tieBreak[i] = (BYTE)(0x90 + i);
    }
    for (int i = 0; i < 4; ++i) {
        image.playerSetup[i].value0 = (unsigned)(i + 1);
        image.playerSetup[i].value1 = (unsigned)(i + 2);
        image.playerSetup[i].value2 = (unsigned)(i & 3);
        image.playerSetup[i].value3 = (unsigned)((i + 1) & 3);
        image.playerSetup[i].value4 = (unsigned)((i + 2) & 7);
        image.playerSetup[i].reserved = (unsigned)(0x10000 + i);
        image.playerIdentifiers[i].name[0] = (char)('A' + i);
        image.playerIdentifiers[i].flags = 0x11223344u + i;
        for (int j = 0; j < 4; ++j)
            image.playerIdentifiers[i].counters[j] = (BYTE)(0x10 * (j + 1) + i);
        for (int j = 0; j < (int)sizeof(image.playerCars[i].data); ++j)
            image.playerCars[i].data[j] = (BYTE)(i * 7 + j);
        for (int j = 0; j < (int)sizeof(image.playerCarSetups[i]); ++j)
            ((BYTE *)&image.playerCarSetups[i])[j] = (BYTE)(i * 3 + j);
    }
    for (int i = 0; i < 20; ++i) {
        image.settingPairs[i][0] = 0x01020304 + i;
        image.settingPairs[i][1] = -2 - i;
        image.primaryValues[i] = (int)(0x11111111u * (unsigned)(i + 1));
        image.values[i] = -0x01010101 * (i + 1);
        image.secondaryValues[i] = 0x22222222 + i;
        image.selectionFlags[i] = (BYTE)(0x50 + i);
    }
    image.selectionState = 3;
    image.stage.country = 7;
    image.stage.stage = 9;
    image.stage.reserved = 0x12345;
}

// Every game-visible field; the signature block is metadata and is zero after
// an unpack.
void Compare(const SavedGameImage &expected, const SavedGameImage &actual)
{
    const SavedGameImage &a = expected;
    const SavedGameImage &b = actual;

    static const unsigned char zero[sizeof(a.header)] = {0};
    Check(memcmp(b.header, zero, sizeof(zero)) == 0, "header is cleared after unpack");
    Check(memcmp(a.name, b.name, sizeof(a.name)) == 0, "name");
    Check(a.options.mode == b.options.mode && a.options.difficulty == b.options.difficulty &&
              a.options.playerCount == b.options.playerCount &&
              a.options.multiplayer == b.options.multiplayer && a.options.reserved == b.options.reserved,
          "options");
    for (int i = 0; i < 16; ++i) {
        Check(a.drivers[i].car == b.drivers[i].car && a.drivers[i].entryFlag == b.drivers[i].entryFlag &&
                  a.drivers[i].seconds == b.drivers[i].seconds &&
                  a.drivers[i].reserved == b.drivers[i].reserved && a.drivers[i].time == b.drivers[i].time,
              "driver");
    }
    for (int i = 0; i < 4; ++i) {
        Check(a.playerSetup[i].value0 == b.playerSetup[i].value0 &&
                  a.playerSetup[i].value1 == b.playerSetup[i].value1 &&
                  a.playerSetup[i].value2 == b.playerSetup[i].value2 &&
                  a.playerSetup[i].value3 == b.playerSetup[i].value3 &&
                  a.playerSetup[i].value4 == b.playerSetup[i].value4 &&
                  a.playerSetup[i].reserved == b.playerSetup[i].reserved,
              "setup");
        Check(memcmp(a.playerIdentifiers[i].name, b.playerIdentifiers[i].name, 4) == 0 &&
                  a.playerIdentifiers[i].flags == b.playerIdentifiers[i].flags &&
                  memcmp(a.playerIdentifiers[i].counters, b.playerIdentifiers[i].counters, 4) == 0,
              "identifier");
        Check(memcmp(a.playerCars[i].data, b.playerCars[i].data, sizeof(a.playerCars[i].data)) == 0,
              "playerCars");
        Check(memcmp(&a.playerCarSetups[i], &b.playerCarSetups[i],
                     sizeof(a.playerCarSetups[i])) == 0,
              "playerCarSetups");
    }
    Check(memcmp(a.settingPairs, b.settingPairs, sizeof(a.settingPairs)) == 0, "settingPairs");
    Check(memcmp(a.primaryValues, b.primaryValues, sizeof(a.primaryValues)) == 0, "primaryValues");
    Check(memcmp(a.selectionFlags, b.selectionFlags, sizeof(a.selectionFlags)) == 0, "selectionFlags");
    Check(memcmp(a.values, b.values, sizeof(a.values)) == 0, "values");
    Check(memcmp(a.secondaryValues, b.secondaryValues, sizeof(a.secondaryValues)) == 0, "secondaryValues");
    Check(a.selectionState == b.selectionState, "selectionState");
    Check(a.stage.country == b.stage.country && a.stage.stage == b.stage.stage &&
              a.stage.reserved == b.stage.reserved,
          "stage");
    Check(memcmp(a.tieBreak, b.tieBreak, sizeof(a.tieBreak)) == 0, "tieBreak");
}

void ChecksumVector()
{
    // The standard CRC-32 check value.
    Check(SavedImageDisk_Checksum("123456789", 9) == 0xcbf43926u, "crc32 check value");
    Check(SavedImageDisk_Checksum("", 0) == 0, "crc32 of nothing");
}

void FileLayout(const unsigned char *packed)
{
    Check(memcmp(packed, "CM2A", 4) == 0, "signature");
    Check(RawBE32(packed + 4) == SAVED_IMAGE_AMIGA_VERSION, "version");
    Check(RawBE32(packed + 12) == 0, "flags");
    Check(RawBE32(packed + 8) == SavedImageDisk_Checksum(packed + 0x10, sizeof(SavedGameImage) - 0x10),
          "stored checksum");

    Check(RawBE32(packed + 0x30) == (0x2a | (2u << 7) | (3u << 10) | (1u << 14) | (0x12345u << 15)),
          "options word is big-endian with LSB-first fields");
    Check(RawBE32(packed + 0x34) == (1 | (0u << 6) | (0x40u << 7) | (0x1000u << 14)), "driver word");
    Check(RawBE32(packed + 0x38) == 0x01020304u, "driver time");
    Check(RawBE32(packed + 0xb4) ==
              (1 | (2u << 3) | (0u << 6) | (1u << 8) | (2u << 10) | (0x10000u << 13)),
          "setup word");
    Check(RawBE32(packed + 0xc4 + 4) == 0x11223344u, "identifier flags");
    Check(RawBE32(packed + 0x630) == 0x01020304u, "setting pair");
    Check(RawBE32(packed + 0x7d8) == (7 | (9u << 5) | (0x12345u << 10)), "stage word");
    Check(packed[0x10] == 'A' && packed[0x1b] == 0, "name bytes are verbatim");
    Check(packed[0xf4] == 0 && packed[0xf5] == 1, "opaque car bytes are verbatim");
}

void RoundTripAndLayout()
{
    SavedGameImage image;
    unsigned char packed[sizeof(SavedGameImage)];
    SavedGameImage unpacked;

    Fill(image);
    // Whatever the game left in the header must not reach the file.
    memset(image.header, 0xee, sizeof(image.header));
    Check(SavedImageDisk_PackAs(SAVED_IMAGE_AMIGA, &image, packed, sizeof(packed)) == SAVED_IMAGE_OK, "pack");
    FileLayout(packed);

    unsigned char again[sizeof(SavedGameImage)];
    Check(SavedImageDisk_PackAs(SAVED_IMAGE_AMIGA, &image, again, sizeof(again)) == SAVED_IMAGE_OK, "pack again");
    Check(memcmp(packed, again, sizeof(packed)) == 0, "packing is deterministic");

    memcpy(&unpacked, packed, sizeof(unpacked));
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, &unpacked, sizeof(unpacked)) == SAVED_IMAGE_OK,
          "unpack in place");
    memset(image.header, 0, sizeof(image.header));
    Compare(image, unpacked);
}

void RejectsForeignAndDamagedFiles()
{
    SavedGameImage image;
    unsigned char packed[sizeof(SavedGameImage)];
    unsigned char work[sizeof(SavedGameImage)];

    Fill(image);

    // A PC save has no signature.
    Check(SavedImageDisk_PackAs(SAVED_IMAGE_PC, &image, packed, sizeof(packed)) == SAVED_IMAGE_OK, "pack PC");
    memcpy(work, packed, sizeof(work));
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_FOREIGN,
          "PC image is foreign");
    Check(memcmp(work, packed, sizeof(work)) == 0, "a foreign image is left untouched");

    // An all-zero file is not an Amiga save either.
    memset(work, 0, sizeof(work));
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_FOREIGN,
          "zero file is foreign");

    Check(SavedImageDisk_PackAs(SAVED_IMAGE_AMIGA, &image, packed, sizeof(packed)) == SAVED_IMAGE_OK, "pack");

    // Every byte after the header is covered by the checksum.
    for (size_t i = 0x10; i < sizeof(packed); ++i) {
        memcpy(work, packed, sizeof(work));
        work[i] ^= 0x01;
        Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_CORRUPT,
              "a flipped payload bit is corrupt");
        work[i] ^= 0x01;
        Check(memcmp(work, packed, sizeof(work)) == 0, "a corrupt image is left untouched");
    }

    // A damaged checksum field.
    memcpy(work, packed, sizeof(work));
    work[8] ^= 0x80;
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_CORRUPT,
          "a damaged checksum is corrupt");

    // A damaged signature byte is foreign, a different version or any flag is
    // unsupported (the payload is not interpreted).
    for (int i = 0; i < 4; ++i) {
        memcpy(work, packed, sizeof(work));
        work[i] ^= 0x20;
        Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_FOREIGN,
              "a damaged signature is foreign");
    }
    memcpy(work, packed, sizeof(work));
    work[7] = (unsigned char)(SAVED_IMAGE_AMIGA_VERSION + 1);
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_UNSUPPORTED,
          "a newer version is unsupported");
    memcpy(work, packed, sizeof(work));
    work[4] = 0x01;
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_UNSUPPORTED,
          "a huge version is unsupported");
    memcpy(work, packed, sizeof(work));
    work[15] = 1;
    Check(SavedImageDisk_UnpackAs(SAVED_IMAGE_AMIGA, work, sizeof(work)) == SAVED_IMAGE_UNSUPPORTED,
          "unknown flags are unsupported");
}

void RejectsBadArguments()
{
    SavedGameImage image;
    unsigned char packed[sizeof(SavedGameImage)];

    Fill(image);
    for (int f = SAVED_IMAGE_PC; f <= SAVED_IMAGE_AMIGA; ++f) {
        const SavedImageFormat format = (SavedImageFormat)f;
        Check(SavedImageDisk_PackAs(format, NULL, packed, sizeof(packed)) == SAVED_IMAGE_BAD_ARGUMENT,
              "pack NULL image");
        Check(SavedImageDisk_PackAs(format, &image, NULL, sizeof(packed)) == SAVED_IMAGE_BAD_ARGUMENT,
              "pack NULL bytes");
        Check(SavedImageDisk_PackAs(format, &image, packed, sizeof(packed) - 1) == SAVED_IMAGE_BAD_SIZE,
              "pack short buffer");
        Check(SavedImageDisk_PackAs(format, &image, packed, sizeof(packed) + 1) == SAVED_IMAGE_BAD_SIZE,
              "pack long buffer");
        Check(SavedImageDisk_UnpackAs(format, NULL, sizeof(packed)) == SAVED_IMAGE_BAD_ARGUMENT,
              "unpack NULL");
        Check(SavedImageDisk_UnpackAs(format, packed, sizeof(packed) - 1) == SAVED_IMAGE_BAD_SIZE,
              "unpack short file");
        Check(SavedImageDisk_UnpackAs(format, packed, sizeof(packed) + 1) == SAVED_IMAGE_BAD_SIZE,
              "unpack long file");
    }
}

#ifdef OPENCMR2_TEST_FORCE_AMIGA_SAVES
int rejectCalls;
SavedImageStatus lastRejected;

void OnReject(SavedImageStatus status)
{
    ++rejectCalls;
    lastRejected = status;
}

void NativeEntryPoints()
{
    SavedGameImage image;
    unsigned char packed[sizeof(SavedGameImage)];
    unsigned char work[sizeof(SavedGameImage)];

    Check(SavedImageDisk_NativeFormat() == SAVED_IMAGE_AMIGA, "native format is Amiga here");

    Fill(image);
    Check(SavedImageDisk_Pack(&image, packed, sizeof(packed)), "native pack");
    FileLayout(packed);

    SavedImageDisk_SetRejectHandler(OnReject);

    memcpy(work, packed, sizeof(work));
    Check(SavedImageDisk_Unpack(work, sizeof(work)), "native unpack");
    Check(rejectCalls == 0, "a valid image is not reported");

    Check(SavedImageDisk_PackAs(SAVED_IMAGE_PC, &image, work, sizeof(work)) == SAVED_IMAGE_OK, "pack PC");
    Check(!SavedImageDisk_Unpack(work, sizeof(work)), "native unpack refuses a PC image");
    Check(rejectCalls == 1 && lastRejected == SAVED_IMAGE_FOREIGN, "the PC image is reported as foreign");

    memcpy(work, packed, sizeof(work));
    work[0x100] ^= 1;
    Check(!SavedImageDisk_Unpack(work, sizeof(work)), "native unpack refuses a damaged image");
    Check(rejectCalls == 2 && lastRejected == SAVED_IMAGE_CORRUPT, "the damaged image is reported");

    memcpy(work, packed, sizeof(work));
    work[7] = 2;
    Check(!SavedImageDisk_Unpack(work, sizeof(work)), "native unpack refuses a newer image");
    Check(rejectCalls == 3 && lastRejected == SAVED_IMAGE_UNSUPPORTED, "the newer image is reported");

    // Caller mistakes are not file rejections.
    Check(!SavedImageDisk_Unpack(NULL, sizeof(work)), "native unpack NULL");
    Check(!SavedImageDisk_Unpack(work, sizeof(work) - 1), "native unpack short file");
    Check(rejectCalls == 3, "bad arguments are not reported");

    SavedImageDisk_SetRejectHandler(NULL);
    Check(!SavedImageDisk_Unpack(work, 0) && rejectCalls == 3, "no handler, no call");
    memset(work, 0, sizeof(work));
    Check(!SavedImageDisk_Unpack(work, sizeof(work)), "rejection without a handler");
}
#endif

} // namespace

int main()
{
    ChecksumVector();
    RoundTripAndLayout();
    RejectsForeignAndDamagedFiles();
    RejectsBadArguments();
#ifdef OPENCMR2_TEST_FORCE_AMIGA_SAVES
    NativeEntryPoints();
#endif

    if (failures != 0) {
        std::fprintf(stderr, "save_image_amiga: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("save_image_amiga: OK\n");
    return 0;
}
