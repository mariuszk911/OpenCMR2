#include "port/types.h"
#include "port/save_image_disk.h"

#include "SavedGame.h"
#include "port/endian.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static_assert(sizeof(SavedGameImage) == 0x7f4, "saved-game file layout");
static_assert(sizeof(SavedGameOptions) == 4, "saved-game options layout");
static_assert(sizeof(SavedGameDriver) == 8, "saved-game driver layout");
static_assert(sizeof(DriverProfileSetup) == 4, "saved-game profile layout");
static_assert(sizeof(SavedGameStage) == 4, "saved-game stage layout");
static_assert(offsetof(SavedGameImage, options) == 0x30, "saved-game options offset");
static_assert(offsetof(SavedGameImage, drivers) == 0x34, "saved-game drivers offset");
static_assert(offsetof(SavedGameImage, playerSetup) == 0xb4, "saved-game setup offset");
static_assert(offsetof(SavedGameImage, playerIdentifiers) == 0xc4, "saved-game identifiers offset");
static_assert(offsetof(SavedGameImage, playerCars) == 0xf4, "saved-game cars offset");
static_assert(offsetof(SavedGameImage, playerControllers) == 0x614, "saved-game controllers offset");
static_assert(offsetof(SavedGameImage, settingPairs) == 0x630, "saved-game pairs offset");
static_assert(offsetof(SavedGameImage, primaryValues) == 0x6d0, "saved-game primary offset");
static_assert(offsetof(SavedGameImage, selectionFlags) == 0x720, "saved-game flags offset");
static_assert(offsetof(SavedGameImage, values) == 0x734, "saved-game values offset");
static_assert(offsetof(SavedGameImage, secondaryValues) == 0x784, "saved-game secondary offset");
static_assert(offsetof(SavedGameImage, selectionState) == 0x7d4, "saved-game state offset");
static_assert(offsetof(SavedGameImage, stage) == 0x7d8, "saved-game stage offset");
static_assert(offsetof(SavedGameImage, tieBreak) == 0x7dc, "saved-game tie-break offset");

namespace {

void UnpackIntArray(unsigned char *bytes, size_t offset, int *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        values[i] = (int)ReadLE32(bytes + offset + i * sizeof(uint32_t));
}

} // namespace

bool SavedImageDisk_Unpack(void *image, size_t size)
{
    if (image == NULL || size != sizeof(SavedGameImage))
        return false;

    SavedGameImage *out = (SavedGameImage *)image;
    unsigned char *bytes = (unsigned char *)image;

    uint32_t word = ReadLE32(bytes + offsetof(SavedGameImage, options));
    out->options.mode = (unsigned)(word & 0x7f);
    out->options.difficulty = (unsigned)((word >> 7) & 0x7);
    out->options.playerCount = (unsigned)((word >> 10) & 0xf);
    out->options.multiplayer = (unsigned)((word >> 14) & 0x1);
    out->options.reserved = (unsigned)((word >> 15) & 0x1ffff);

    for (int i = 0; i < 16; ++i) {
        unsigned char *driver = bytes + offsetof(SavedGameImage, drivers) + i * sizeof(SavedGameDriver);
        word = ReadLE32(driver);
        out->drivers[i].car = (unsigned)(word & 0x3f);
        out->drivers[i].entryFlag = (unsigned)((word >> 6) & 0x1);
        out->drivers[i].seconds = (unsigned)((word >> 7) & 0x7f);
        out->drivers[i].reserved = (unsigned)((word >> 14) & 0x3ffff);
        out->drivers[i].time = (int)ReadLE32(driver + offsetof(SavedGameDriver, time));
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *setup = bytes + offsetof(SavedGameImage, playerSetup) + i * sizeof(DriverProfileSetup);
        word = ReadLE32(setup);
        out->playerSetup[i].value0 = (unsigned)(word & 0x7);
        out->playerSetup[i].value1 = (unsigned)((word >> 3) & 0x7);
        out->playerSetup[i].value2 = (unsigned)((word >> 6) & 0x3);
        out->playerSetup[i].value3 = (unsigned)((word >> 8) & 0x3);
        out->playerSetup[i].value4 = (unsigned)((word >> 10) & 0x7);
        out->playerSetup[i].reserved = (unsigned)((word >> 13) & 0x7ffff);
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *identifier = bytes + offsetof(SavedGameImage, playerIdentifiers) +
                                    i * sizeof(DriverProfileIdentifier);
        out->playerIdentifiers[i].flags =
            (unsigned)ReadLE32(identifier + offsetof(DriverProfileIdentifier, flags));
    }

    UnpackIntArray(bytes, offsetof(SavedGameImage, settingPairs), &out->settingPairs[0][0], 20 * 2);
    UnpackIntArray(bytes, offsetof(SavedGameImage, primaryValues), out->primaryValues, 20);
    UnpackIntArray(bytes, offsetof(SavedGameImage, values), out->values, 20);
    UnpackIntArray(bytes, offsetof(SavedGameImage, secondaryValues), out->secondaryValues, 20);

    word = ReadLE32(bytes + offsetof(SavedGameImage, stage));
    out->stage.country = (unsigned)(word & 0x1f);
    out->stage.stage = (unsigned)((word >> 5) & 0x1f);
    out->stage.reserved = (unsigned)((word >> 10) & 0x3fffff);

    return true;
}

bool SavedImageDisk_Pack(const void *image, void *bytes, size_t size)
{
    if (image == NULL || bytes == NULL || size != sizeof(SavedGameImage))
        return false;

    const SavedGameImage *in = (const SavedGameImage *)image;
    unsigned char *out = (unsigned char *)bytes;

    memcpy(out, in, sizeof(SavedGameImage));

    uint32_t word = (uint32_t)(in->options.mode & 0x7f) |
                    (uint32_t)(in->options.difficulty & 0x7) << 7 |
                    (uint32_t)(in->options.playerCount & 0xf) << 10 |
                    (uint32_t)(in->options.multiplayer & 0x1) << 14 |
                    (uint32_t)(in->options.reserved & 0x1ffff) << 15;
    WriteLE32(out + offsetof(SavedGameImage, options), word);

    for (int i = 0; i < 16; ++i) {
        unsigned char *driver = out + offsetof(SavedGameImage, drivers) + i * sizeof(SavedGameDriver);
        word = (uint32_t)(in->drivers[i].car & 0x3f) |
               (uint32_t)(in->drivers[i].entryFlag & 0x1) << 6 |
               (uint32_t)(in->drivers[i].seconds & 0x7f) << 7 |
               (uint32_t)(in->drivers[i].reserved & 0x3ffff) << 14;
        WriteLE32(driver, word);
        WriteLE32(driver + offsetof(SavedGameDriver, time), (uint32_t)in->drivers[i].time);
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *setup = out + offsetof(SavedGameImage, playerSetup) + i * sizeof(DriverProfileSetup);
        word = (uint32_t)(in->playerSetup[i].value0 & 0x7) |
               (uint32_t)(in->playerSetup[i].value1 & 0x7) << 3 |
               (uint32_t)(in->playerSetup[i].value2 & 0x3) << 6 |
               (uint32_t)(in->playerSetup[i].value3 & 0x3) << 8 |
               (uint32_t)(in->playerSetup[i].value4 & 0x7) << 10 |
               (uint32_t)(in->playerSetup[i].reserved & 0x7ffff) << 13;
        WriteLE32(setup, word);
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *identifier = out + offsetof(SavedGameImage, playerIdentifiers) +
                                    i * sizeof(DriverProfileIdentifier);
        identifier += offsetof(DriverProfileIdentifier, flags);
        WriteLE32(identifier, (uint32_t)in->playerIdentifiers[i].flags);
    }

    for (int i = 0; i < 20 * 2; ++i)
        WriteLE32(out + offsetof(SavedGameImage, settingPairs) + i * sizeof(uint32_t),
                  (uint32_t)in->settingPairs[i / 2][i % 2]);
    for (int i = 0; i < 20; ++i) {
        WriteLE32(out + offsetof(SavedGameImage, primaryValues) + i * sizeof(uint32_t),
                  (uint32_t)in->primaryValues[i]);
        WriteLE32(out + offsetof(SavedGameImage, values) + i * sizeof(uint32_t),
                  (uint32_t)in->values[i]);
        WriteLE32(out + offsetof(SavedGameImage, secondaryValues) + i * sizeof(uint32_t),
                  (uint32_t)in->secondaryValues[i]);
    }

    word = (uint32_t)(in->stage.country & 0x1f) |
           (uint32_t)(in->stage.stage & 0x1f) << 5 |
           (uint32_t)(in->stage.reserved & 0x3fffff) << 10;
    WriteLE32(out + offsetof(SavedGameImage, stage), word);

    return true;
}
