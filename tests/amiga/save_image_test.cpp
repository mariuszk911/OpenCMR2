#include "SavedGame.h"
#include "port/save_image_disk.h"

#include <stdio.h>
#include <string.h>

static unsigned long RawBE32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

static int rejected;
static SavedImageStatus lastRejected;

static void OnReject(SavedImageStatus status)
{
    ++rejected;
    lastRejected = status;
}

int main(void)
{
    SavedGameImage image;
    SavedGameImage out;
    unsigned char packed[sizeof(SavedGameImage)];

    memset(&image, 0, sizeof(image));
    strcpy(image.name, "Saved Game");
    image.options.mode = 0x2a;
    image.options.difficulty = 5;
    image.options.playerCount = 3;
    image.options.multiplayer = 1;
    image.options.reserved = 0x12345;
    image.drivers[0].car = 1;
    image.drivers[0].entryFlag = 1;
    image.drivers[0].seconds = 0x40;
    image.drivers[0].reserved = 0x1000;
    image.drivers[0].time = 0x01020304;
    image.playerSetup[0].value0 = 1;
    image.playerSetup[0].value1 = 2;
    image.playerSetup[0].value3 = 1;
    image.playerSetup[0].value4 = 2;
    image.playerSetup[0].reserved = 0x10000;
    image.playerIdentifiers[0].flags = 0x11223344;
    image.settingPairs[0][0] = 0x01020304;
    image.primaryValues[0] = -7;
    image.stage.country = 7;
    image.stage.stage = 9;
    image.stage.reserved = 0x12345;

    if (!SavedImageDisk_Pack(&image, packed, sizeof(packed))) {
        printf("save_image: FAIL pack\n");
        return 1;
    }
    if (SavedImageDisk_NativeFormat() != SAVED_IMAGE_AMIGA) {
        printf("save_image: FAIL native format is not the Amiga one\n");
        return 1;
    }
    if (memcmp(packed, "CM2A", 4) != 0 || RawBE32(packed + 4) != SAVED_IMAGE_AMIGA_VERSION ||
        RawBE32(packed + 12) != 0 ||
        RawBE32(packed + 8) != SavedImageDisk_Checksum(packed + 0x10, sizeof(packed) - 0x10)) {
        printf("save_image: FAIL header\n");
        return 1;
    }
    if (RawBE32(packed + 0x30) != (0x2a | 5ul << 7 | 3ul << 10 | 1ul << 14 | 0x12345ul << 15)) {
        printf("save_image: FAIL options word\n");
        return 1;
    }
    if (RawBE32(packed + 0x38) != 0x01020304ul) {
        printf("save_image: FAIL driver time\n");
        return 1;
    }
    if (RawBE32(packed + 0x630) != 0x01020304ul) {
        printf("save_image: FAIL pair\n");
        return 1;
    }
    if (RawBE32(packed + 0x7d8) != (7ul | 9ul << 5 | 0x12345ul << 10)) {
        printf("save_image: FAIL stage word\n");
        return 1;
    }

    memcpy(&out, packed, sizeof(out));
    if (!SavedImageDisk_Unpack(&out, sizeof(out))) {
        printf("save_image: FAIL unpack\n");
        return 1;
    }
    if (out.options.mode != 0x2a || out.options.difficulty != 5 ||
        out.options.playerCount != 3 || out.options.multiplayer != 1 ||
        out.options.reserved != 0x12345) {
        printf("save_image: FAIL options round trip\n");
        return 1;
    }
    if (out.drivers[0].car != 1 || out.drivers[0].entryFlag != 1 ||
        out.drivers[0].seconds != 0x40 || out.drivers[0].reserved != 0x1000 ||
        out.drivers[0].time != 0x01020304) {
        printf("save_image: FAIL driver round trip\n");
        return 1;
    }
    if (out.playerSetup[0].value0 != 1 || out.playerSetup[0].value1 != 2 ||
        out.playerSetup[0].value3 != 1 || out.playerSetup[0].value4 != 2 ||
        out.playerSetup[0].reserved != 0x10000) {
        printf("save_image: FAIL setup round trip\n");
        return 1;
    }
    if (out.playerIdentifiers[0].flags != 0x11223344 ||
        out.settingPairs[0][0] != 0x01020304 || out.primaryValues[0] != -7 ||
        out.stage.country != 7 || out.stage.stage != 9 || out.stage.reserved != 0x12345) {
        printf("save_image: FAIL scalar round trip\n");
        return 1;
    }
    if (strcmp(out.name, "Saved Game") != 0) {
        printf("save_image: FAIL name\n");
        return 1;
    }

    /* A damaged, a newer and a PC-format file are refused and reported, and the
       refused buffer is left as it was. */
    SavedImageDisk_SetRejectHandler(OnReject);
    {
        unsigned char work[sizeof(SavedGameImage)];

        memcpy(work, packed, sizeof(work));
        work[0x200] ^= 1;
        if (SavedImageDisk_Unpack(work, sizeof(work)) || rejected != 1 || lastRejected != SAVED_IMAGE_CORRUPT) {
            printf("save_image: FAIL corrupt image accepted\n");
            return 1;
        }
        memcpy(work, packed, sizeof(work));
        work[7] = 2;
        if (SavedImageDisk_Unpack(work, sizeof(work)) || rejected != 2 || lastRejected != SAVED_IMAGE_UNSUPPORTED) {
            printf("save_image: FAIL newer image accepted\n");
            return 1;
        }
        if (SavedImageDisk_PackAs(SAVED_IMAGE_PC, &image, work, sizeof(work)) != SAVED_IMAGE_OK) {
            printf("save_image: FAIL pack PC\n");
            return 1;
        }
        {
            unsigned char pc[sizeof(SavedGameImage)];
            memcpy(pc, work, sizeof(pc));
            if (SavedImageDisk_Unpack(work, sizeof(work)) || rejected != 3 || lastRejected != SAVED_IMAGE_FOREIGN ||
                memcmp(work, pc, sizeof(work)) != 0) {
                printf("save_image: FAIL PC image accepted\n");
                return 1;
            }
        }
    }
    SavedImageDisk_SetRejectHandler(NULL);

    printf("save_image: OK\n");
    return 0;
}
