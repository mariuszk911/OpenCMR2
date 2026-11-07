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

// How the 32-bit words of one file format are stored.
struct WordOrder {
    uint32_t (*load)(const void *source);
    void (*store)(void *destination, uint32_t value);
};

const WordOrder kLittleEndian = {ReadLE32, WriteLE32};
const WordOrder kBigEndian = {ReadBE32, WriteBE32};

const unsigned char kAmigaSignature[4] = {'C', 'M', '2', 'A'};
const size_t kAmigaChecksumOffset = 8;
const size_t kAmigaFlagsOffset = 12;
const size_t kAmigaHeaderSize = sizeof(SavedGameImage::header);

SavedImageRejectHandler s_rejectHandler;

const WordOrder &OrderOf(SavedImageFormat format)
{
    return format == SAVED_IMAGE_AMIGA ? kBigEndian : kLittleEndian;
}

void UnpackIntArray(const WordOrder &order, unsigned char *bytes, size_t offset, int *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        values[i] = (int)order.load(bytes + offset + i * sizeof(uint32_t));
}

void PackIntArray(const WordOrder &order, unsigned char *bytes, size_t offset, const int *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        order.store(bytes + offset + i * sizeof(uint32_t), (uint32_t)values[i]);
}

// Checks the Amiga signature, version, flags and checksum without changing the
// image. The checksum covers everything after the header.
SavedImageStatus VerifyAmigaHeader(const unsigned char *bytes)
{
    if (memcmp(bytes, kAmigaSignature, sizeof(kAmigaSignature)) != 0)
        return SAVED_IMAGE_FOREIGN;
    if (ReadBE32(bytes + sizeof(kAmigaSignature)) != SAVED_IMAGE_AMIGA_VERSION ||
        ReadBE32(bytes + kAmigaFlagsOffset) != 0)
        return SAVED_IMAGE_UNSUPPORTED;
    if (ReadBE32(bytes + kAmigaChecksumOffset) !=
        SavedImageDisk_Checksum(bytes + kAmigaHeaderSize, sizeof(SavedGameImage) - kAmigaHeaderSize))
        return SAVED_IMAGE_CORRUPT;
    return SAVED_IMAGE_OK;
}

void WriteAmigaHeader(unsigned char *bytes)
{
    memcpy(bytes, kAmigaSignature, sizeof(kAmigaSignature));
    WriteBE32(bytes + sizeof(kAmigaSignature), SAVED_IMAGE_AMIGA_VERSION);
    WriteBE32(bytes + kAmigaFlagsOffset, 0);
    WriteBE32(bytes + kAmigaChecksumOffset,
              SavedImageDisk_Checksum(bytes + kAmigaHeaderSize, sizeof(SavedGameImage) - kAmigaHeaderSize));
}

void UnpackFields(const WordOrder &order, void *image)
{
    SavedGameImage *out = (SavedGameImage *)image;
    unsigned char *bytes = (unsigned char *)image;

    uint32_t word = order.load(bytes + offsetof(SavedGameImage, options));
    out->options.mode = (unsigned)(word & 0x7f);
    out->options.difficulty = (unsigned)((word >> 7) & 0x7);
    out->options.playerCount = (unsigned)((word >> 10) & 0xf);
    out->options.multiplayer = (unsigned)((word >> 14) & 0x1);
    out->options.reserved = (unsigned)((word >> 15) & 0x1ffff);

    for (int i = 0; i < 16; ++i) {
        unsigned char *driver = bytes + offsetof(SavedGameImage, drivers) + i * sizeof(SavedGameDriver);
        word = order.load(driver);
        out->drivers[i].car = (unsigned)(word & 0x3f);
        out->drivers[i].entryFlag = (unsigned)((word >> 6) & 0x1);
        out->drivers[i].seconds = (unsigned)((word >> 7) & 0x7f);
        out->drivers[i].reserved = (unsigned)((word >> 14) & 0x3ffff);
        out->drivers[i].time = (int)order.load(driver + offsetof(SavedGameDriver, time));
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *setup = bytes + offsetof(SavedGameImage, playerSetup) + i * sizeof(DriverProfileSetup);
        word = order.load(setup);
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
            (unsigned)order.load(identifier + offsetof(DriverProfileIdentifier, flags));
    }

    UnpackIntArray(order, bytes, offsetof(SavedGameImage, settingPairs), &out->settingPairs[0][0], 20 * 2);
    UnpackIntArray(order, bytes, offsetof(SavedGameImage, primaryValues), out->primaryValues, 20);
    UnpackIntArray(order, bytes, offsetof(SavedGameImage, values), out->values, 20);
    UnpackIntArray(order, bytes, offsetof(SavedGameImage, secondaryValues), out->secondaryValues, 20);

    word = order.load(bytes + offsetof(SavedGameImage, stage));
    out->stage.country = (unsigned)(word & 0x1f);
    out->stage.stage = (unsigned)((word >> 5) & 0x1f);
    out->stage.reserved = (unsigned)((word >> 10) & 0x3fffff);
}

void PackFields(const WordOrder &order, const void *image, unsigned char *out)
{
    const SavedGameImage *in = (const SavedGameImage *)image;

    memcpy(out, in, sizeof(SavedGameImage));

    uint32_t word = (uint32_t)(in->options.mode & 0x7f) |
                    (uint32_t)(in->options.difficulty & 0x7) << 7 |
                    (uint32_t)(in->options.playerCount & 0xf) << 10 |
                    (uint32_t)(in->options.multiplayer & 0x1) << 14 |
                    (uint32_t)(in->options.reserved & 0x1ffff) << 15;
    order.store(out + offsetof(SavedGameImage, options), word);

    for (int i = 0; i < 16; ++i) {
        unsigned char *driver = out + offsetof(SavedGameImage, drivers) + i * sizeof(SavedGameDriver);
        word = (uint32_t)(in->drivers[i].car & 0x3f) |
               (uint32_t)(in->drivers[i].entryFlag & 0x1) << 6 |
               (uint32_t)(in->drivers[i].seconds & 0x7f) << 7 |
               (uint32_t)(in->drivers[i].reserved & 0x3ffff) << 14;
        order.store(driver, word);
        order.store(driver + offsetof(SavedGameDriver, time), (uint32_t)in->drivers[i].time);
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *setup = out + offsetof(SavedGameImage, playerSetup) + i * sizeof(DriverProfileSetup);
        word = (uint32_t)(in->playerSetup[i].value0 & 0x7) |
               (uint32_t)(in->playerSetup[i].value1 & 0x7) << 3 |
               (uint32_t)(in->playerSetup[i].value2 & 0x3) << 6 |
               (uint32_t)(in->playerSetup[i].value3 & 0x3) << 8 |
               (uint32_t)(in->playerSetup[i].value4 & 0x7) << 10 |
               (uint32_t)(in->playerSetup[i].reserved & 0x7ffff) << 13;
        order.store(setup, word);
    }

    for (int i = 0; i < 4; ++i) {
        unsigned char *identifier = out + offsetof(SavedGameImage, playerIdentifiers) +
                                    i * sizeof(DriverProfileIdentifier);
        order.store(identifier + offsetof(DriverProfileIdentifier, flags),
                    (uint32_t)in->playerIdentifiers[i].flags);
    }

    PackIntArray(order, out, offsetof(SavedGameImage, settingPairs), &in->settingPairs[0][0], 20 * 2);
    PackIntArray(order, out, offsetof(SavedGameImage, primaryValues), in->primaryValues, 20);
    PackIntArray(order, out, offsetof(SavedGameImage, values), in->values, 20);
    PackIntArray(order, out, offsetof(SavedGameImage, secondaryValues), in->secondaryValues, 20);

    word = (uint32_t)(in->stage.country & 0x1f) |
           (uint32_t)(in->stage.stage & 0x1f) << 5 |
           (uint32_t)(in->stage.reserved & 0x3fffff) << 10;
    order.store(out + offsetof(SavedGameImage, stage), word);
}

} // namespace

uint32_t SavedImageDisk_Checksum(const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = 0xffffffffu;

    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

SavedImageStatus SavedImageDisk_UnpackAs(SavedImageFormat format, void *image, size_t size)
{
    if (image == NULL)
        return SAVED_IMAGE_BAD_ARGUMENT;
    if (size != sizeof(SavedGameImage))
        return SAVED_IMAGE_BAD_SIZE;

    unsigned char *bytes = (unsigned char *)image;
    if (format == SAVED_IMAGE_AMIGA) {
        const SavedImageStatus status = VerifyAmigaHeader(bytes);
        if (status != SAVED_IMAGE_OK)
            return status;
    }
    UnpackFields(OrderOf(format), image);
    // The signature block is file metadata, not game state.
    if (format == SAVED_IMAGE_AMIGA)
        memset(bytes, 0, kAmigaHeaderSize);
    return SAVED_IMAGE_OK;
}

SavedImageStatus SavedImageDisk_PackAs(SavedImageFormat format, const void *image,
                                       void *bytes, size_t size)
{
    if (image == NULL || bytes == NULL)
        return SAVED_IMAGE_BAD_ARGUMENT;
    if (size != sizeof(SavedGameImage))
        return SAVED_IMAGE_BAD_SIZE;

    unsigned char *out = (unsigned char *)bytes;
    PackFields(OrderOf(format), image, out);
    if (format == SAVED_IMAGE_AMIGA)
        WriteAmigaHeader(out);
    return SAVED_IMAGE_OK;
}

SavedImageFormat SavedImageDisk_NativeFormat(void)
{
#if defined(OPENCMR2_BIG_ENDIAN) || defined(OPENCMR2_TEST_FORCE_AMIGA_SAVES)
    return SAVED_IMAGE_AMIGA;
#else
    return SAVED_IMAGE_PC;
#endif
}

void SavedImageDisk_SetRejectHandler(SavedImageRejectHandler handler)
{
    s_rejectHandler = handler;
}

bool SavedImageDisk_Unpack(void *image, size_t size)
{
    const SavedImageStatus status = SavedImageDisk_UnpackAs(SavedImageDisk_NativeFormat(), image, size);
    if (status == SAVED_IMAGE_OK)
        return true;
    if (s_rejectHandler != NULL &&
        (status == SAVED_IMAGE_FOREIGN || status == SAVED_IMAGE_UNSUPPORTED ||
         status == SAVED_IMAGE_CORRUPT))
        s_rejectHandler(status);
    return false;
}

bool SavedImageDisk_Pack(const void *image, void *bytes, size_t size)
{
    return SavedImageDisk_PackAs(SavedImageDisk_NativeFormat(), image, bytes, size) == SAVED_IMAGE_OK;
}
