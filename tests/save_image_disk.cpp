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

[[maybe_unused]] unsigned long RawLE32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
           (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

void Fill(SavedGameImage &image)
{
    memset(&image, 0, sizeof(image));
    for (int i = 0; i < 0x10; ++i)
        image.header[i] = (BYTE)(0xa0 + i);
    strcpy(image.name, "Saved Game");

    image.options.mode = 0x2a;
    image.options.difficulty = 5;
    image.options.playerCount = 3;
    image.options.multiplayer = 1;
    image.options.reserved = 0x12345;

    for (int i = 0; i < 16; ++i) {
        image.drivers[i].car = (unsigned)(i + 1);
        image.drivers[i].entryFlag = (unsigned)(i & 1);
        image.drivers[i].seconds = (unsigned)(0x40 + i);
        image.drivers[i].reserved = (unsigned)(0x1000 + i);
        image.drivers[i].time = 0x01020304 + i;
        image.tieBreak[i] = (BYTE)(0x30 + i);
    }
    for (int i = 0; i < 4; ++i) {
        image.playerSetup[i].value0 = (unsigned)(i + 1);
        image.playerSetup[i].value1 = (unsigned)(i + 2);
        image.playerSetup[i].value2 = (unsigned)(i & 3);
        image.playerSetup[i].value3 = (unsigned)((i + 1) & 3);
        image.playerSetup[i].value4 = (unsigned)((i + 2) & 7);
        image.playerSetup[i].reserved = (unsigned)(0x10000 + i);
        image.playerIdentifiers[i].name[0] = (char)('A' + i);
        image.playerIdentifiers[i].name[1] = 0;
        image.playerIdentifiers[i].flags = 0x11223344u + i;
        image.playerIdentifiers[i].counters[0] = (BYTE)(0x10 + i);
        image.playerIdentifiers[i].counters[1] = (BYTE)(0x20 + i);
        image.playerIdentifiers[i].counters[2] = (BYTE)(0x30 + i);
        image.playerIdentifiers[i].counters[3] = (BYTE)(0x40 + i);
        for (int j = 0; j < (int)sizeof(image.playerCars[i].data); ++j)
            image.playerCars[i].data[j] = (BYTE)(i * 7 + j);
        for (int j = 0; j < (int)sizeof(image.playerControllers[i].data); ++j)
            image.playerControllers[i].data[j] = (BYTE)(i * 3 + j);
    }
    for (int i = 0; i < 20; ++i) {
        image.settingPairs[i][0] = 0x01020304 + i;
        image.settingPairs[i][1] = -2 - i;
        image.primaryValues[i] = 0x11111111 * (i + 1);
        image.values[i] = -0x01010101 * (i + 1);
        image.secondaryValues[i] = 0x22222222 + i;
        image.selectionFlags[i] = (BYTE)(0x50 + i);
    }
    image.selectionState = 0x3;
    for (int i = 0; i < 3; ++i)
        image.reserved1[i] = (BYTE)(0x70 + i);
    image.stage.country = 7;
    image.stage.stage = 9;
    image.stage.reserved = 0x12345;
    for (int i = 0; i < 16; ++i)
        image.tieBreak[i] = (BYTE)(0x90 + i);
    for (int i = 0; i < 8; ++i)
        image.reserved2[i] = (BYTE)(0xb0 + i);
}

void Compare(const SavedGameImage &a, const SavedGameImage &b)
{
    Check(memcmp(a.header, b.header, sizeof(a.header)) == 0, "header bytes");
    Check(memcmp(a.name, b.name, sizeof(a.name)) == 0, "name bytes");
    Check(a.options.mode == b.options.mode, "options.mode");
    Check(a.options.difficulty == b.options.difficulty, "options.difficulty");
    Check(a.options.playerCount == b.options.playerCount, "options.playerCount");
    Check(a.options.multiplayer == b.options.multiplayer, "options.multiplayer");
    Check(a.options.reserved == b.options.reserved, "options.reserved");
    for (int i = 0; i < 16; ++i) {
        Check(a.drivers[i].car == b.drivers[i].car, "driver.car");
        Check(a.drivers[i].entryFlag == b.drivers[i].entryFlag, "driver.entryFlag");
        Check(a.drivers[i].seconds == b.drivers[i].seconds, "driver.seconds");
        Check(a.drivers[i].reserved == b.drivers[i].reserved, "driver.reserved");
        Check(a.drivers[i].time == b.drivers[i].time, "driver.time");
    }
    for (int i = 0; i < 4; ++i) {
        Check(a.playerSetup[i].value0 == b.playerSetup[i].value0, "setup.value0");
        Check(a.playerSetup[i].value1 == b.playerSetup[i].value1, "setup.value1");
        Check(a.playerSetup[i].value2 == b.playerSetup[i].value2, "setup.value2");
        Check(a.playerSetup[i].value3 == b.playerSetup[i].value3, "setup.value3");
        Check(a.playerSetup[i].value4 == b.playerSetup[i].value4, "setup.value4");
        Check(a.playerSetup[i].reserved == b.playerSetup[i].reserved, "setup.reserved");
        Check(memcmp(a.playerIdentifiers[i].name, b.playerIdentifiers[i].name, 4) == 0, "identifier.name");
        Check(a.playerIdentifiers[i].flags == b.playerIdentifiers[i].flags, "identifier.flags");
        Check(memcmp(a.playerIdentifiers[i].counters, b.playerIdentifiers[i].counters, 4) == 0,
              "identifier.counters");
        Check(memcmp(a.playerCars[i].data, b.playerCars[i].data, sizeof(a.playerCars[i].data)) == 0,
              "playerCars");
        Check(memcmp(a.playerControllers[i].data, b.playerControllers[i].data,
                     sizeof(a.playerControllers[i].data)) == 0,
              "playerControllers");
    }
    Check(memcmp(a.settingPairs, b.settingPairs, sizeof(a.settingPairs)) == 0, "settingPairs");
    Check(memcmp(a.primaryValues, b.primaryValues, sizeof(a.primaryValues)) == 0, "primaryValues");
    Check(memcmp(a.selectionFlags, b.selectionFlags, sizeof(a.selectionFlags)) == 0, "selectionFlags");
    Check(memcmp(a.values, b.values, sizeof(a.values)) == 0, "values");
    Check(memcmp(a.secondaryValues, b.secondaryValues, sizeof(a.secondaryValues)) == 0, "secondaryValues");
    Check(a.selectionState == b.selectionState, "selectionState");
    Check(memcmp(a.reserved1, b.reserved1, sizeof(a.reserved1)) == 0, "reserved1");
    Check(a.stage.country == b.stage.country, "stage.country");
    Check(a.stage.stage == b.stage.stage, "stage.stage");
    Check(a.stage.reserved == b.stage.reserved, "stage.reserved");
    Check(memcmp(a.tieBreak, b.tieBreak, sizeof(a.tieBreak)) == 0, "tieBreak");
    Check(memcmp(a.reserved2, b.reserved2, sizeof(a.reserved2)) == 0, "reserved2");
}

} // namespace

int main()
{
    SavedGameImage image;
    SavedGameImage unpacked;
    SavedGameImage inplaceImage;
    unsigned char packed[sizeof(SavedGameImage)];

    Fill(image);
    Check(SavedImageDisk_Pack(&image, packed, sizeof(packed)), "pack");
    memset(&unpacked, 0, sizeof(unpacked));
    Check(SavedImageDisk_Unpack(NULL, sizeof(unpacked)) == false, "unpack rejects null");
    Check(SavedImageDisk_Unpack(&unpacked, sizeof(unpacked) - 1) == false, "unpack rejects size");
    Check(SavedImageDisk_Pack(NULL, packed, sizeof(packed)) == false, "pack rejects null image");
    Check(SavedImageDisk_Pack(&image, NULL, sizeof(packed)) == false, "pack rejects null bytes");
    Check(SavedImageDisk_Pack(&image, packed, sizeof(packed) - 1) == false, "pack rejects size");

#if !defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN)
    // The normal host build writes the original little-endian file bytes.
    Check(RawLE32(packed + 0x30) ==
              (0x2a | (5u << 7) | (3u << 10) | (1u << 14) | (0x12345u << 15)),
          "options disk word");
    Check(RawLE32(packed + 0x34) ==
              (1 | (0u << 6) | (0x40u << 7) | (0x1000u << 14)),
          "driver disk word");
    Check(RawLE32(packed + 0x38) == 0x01020304u, "driver time LE");
    Check(RawLE32(packed + 0xb4) ==
              (1 | (2u << 3) | (0u << 6) | (1u << 8) | (2u << 10) | (0x10000u << 13)),
          "setup disk word");
    Check(RawLE32(packed + 0xc4 + 4) == 0x11223344u, "identifier flags LE");
    Check(RawLE32(packed + 0x630) == 0x01020304u, "setting pair LE");
    Check(RawLE32(packed + 0x7d8) == (7 | (9u << 5) | (0x12345u << 10)), "stage disk word");
    Check(packed[0x10] == 'S' && packed[0x1f] == 0, "name bytes");
    Check(packed[0xf4] == 0, "player car bytes");
#endif

    memcpy(&inplaceImage, packed, sizeof(inplaceImage));
    Check(SavedImageDisk_Unpack(&inplaceImage, sizeof(inplaceImage)), "unpack in place");
    Compare(image, inplaceImage);

    memset(&unpacked, 0, sizeof(unpacked));
    memcpy(&unpacked, packed, sizeof(unpacked));
    Check(SavedImageDisk_Unpack(&unpacked, sizeof(unpacked)), "unpack separate");
    Compare(image, unpacked);

    if (failures != 0) {
        std::fprintf(stderr, "save_image_disk: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("save_image_disk: OK\n");
    return 0;
}
