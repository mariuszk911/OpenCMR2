// Controller.rcf decoder, see port/controller_disk.h.

#include "port/controller_disk.h"

#include "port/types.h"
#include "port/disk_layouts.h"
#include "port/endian.h"

#include <string.h>

extern "C" int ControllerDisk_Decode(void *controllerData, unsigned short *slotMapping,
                                     const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)controllerData;
    unsigned short mapping[CONTROLLER_DISK_MAPPINGS];
    int i;

    if (controllerData == NULL || slotMapping == NULL || file == NULL)
        return 0;
    if (size != CONTROLLER_DISK_FILE_SIZE)
        return 0;

    // Validate first: a rejected file must not overwrite the caller's records.
    mapping[0] = ReadLE16(src + 0x11a0);
    mapping[1] = ReadLE16(src + 0x11a2);
    if (mapping[0] >= CONTROLLER_DISK_RECORDS || mapping[1] >= CONTROLLER_DISK_RECORDS)
        return 0;

    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        Disk_Unpack(dst + (size_t)i * sizeof(ControllerData),
                    src + (size_t)i * g_diskLayout_ControllerData.diskSize,
                    &g_diskLayout_ControllerData);
    }
    slotMapping[0] = mapping[0];
    slotMapping[1] = mapping[1];
    return 1;
}

namespace {

uint16_t ReadBE16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t ReadBE32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

} // namespace

extern "C" int ControllerDisk_Pack(void *file, size_t fileSize,
                                   const void *controllerData, const unsigned short *slotMapping)
{
    const unsigned char *src = (const unsigned char *)controllerData;
    unsigned char *dst = (unsigned char *)file;
    int i;

    if (file == NULL || controllerData == NULL || slotMapping == NULL ||
            fileSize != CONTROLLER_DISK_FILE_SIZE)
        return 0;
    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        Disk_Pack(dst + (size_t)i * g_diskLayout_ControllerData.diskSize,
                  src + (size_t)i * sizeof(ControllerData),
                  &g_diskLayout_ControllerData);
    }
    WriteLE16(dst + 0x11a0, slotMapping[0]);
    WriteLE16(dst + 0x11a2, slotMapping[1]);
    return 1;
}

extern "C" int ControllerDisk_DecodeLegacyBe(void *controllerData, unsigned short *slotMapping,
                                             const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)controllerData;
    unsigned short mapping[CONTROLLER_DISK_MAPPINGS];
    int i;
    int j;

    if (controllerData == NULL || slotMapping == NULL || file == NULL ||
            size != CONTROLLER_DISK_FILE_SIZE)
        return 0;
    mapping[0] = ReadBE16(src + 0x11a0);
    mapping[1] = ReadBE16(src + 0x11a2);
    if (mapping[0] >= CONTROLLER_DISK_RECORDS || mapping[1] >= CONTROLLER_DISK_RECORDS)
        return 0;

    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        const unsigned char *record = src + (size_t)i * g_diskLayout_ControllerData.diskSize;
        unsigned char *native = dst + (size_t)i * sizeof(ControllerData);

        memcpy(native, record, sizeof(ControllerData));
        for (j = 0; j < g_diskLayout_ControllerData.swapCount; j++) {
            const DiskSwap &swap = g_diskLayout_ControllerData.swaps[j];
            unsigned char *field = native + swap.native;

            if (swap.size == 2) {
                uint16_t value = ReadBE16(record + swap.disk);
                memcpy(field, &value, sizeof(value));
            } else if (swap.size == 4) {
                uint32_t value = ReadBE32(record + swap.disk);
                memcpy(field, &value, sizeof(value));
            } else if (swap.size == 8) {
                uint64_t value = ((uint64_t)ReadBE32(record + swap.disk) << 32) |
                                 ReadBE32(record + swap.disk + 4);
                memcpy(field, &value, sizeof(value));
            }
        }
    }
    slotMapping[0] = mapping[0];
    slotMapping[1] = mapping[1];
    return 1;
}

namespace {

int ControllerMappingScore(const ControllerData *records)
{
    int score = 0;
    int i;
    int j;

    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        const unsigned short *mapping = &records[i].field_0x128;
        for (j = 0; j < 11; j++) {
            if (mapping[j] < 256)
                score++;
        }
    }
    return score;
}

} // namespace

extern "C" int ControllerDisk_DecodeAuto(void *controllerData, unsigned short *slotMapping,
                                         const void *file, size_t size)
{
    ControllerData little[CONTROLLER_DISK_RECORDS];
    ControllerData legacy[CONTROLLER_DISK_RECORDS];
    unsigned short littleMapping[CONTROLLER_DISK_MAPPINGS];
    unsigned short legacyMapping[CONTROLLER_DISK_MAPPINGS];
    int littleOk;
    int legacyOk;

    if (controllerData == NULL || slotMapping == NULL || file == NULL)
        return 0;
    littleOk = ControllerDisk_Decode(little, littleMapping, file, size);
    legacyOk = ControllerDisk_DecodeLegacyBe(legacy, legacyMapping, file, size);
    if (!littleOk && !legacyOk)
        return 0;
    if (littleOk && (!legacyOk ||
            ControllerMappingScore(little) >= ControllerMappingScore(legacy))) {
        memcpy(controllerData, little, sizeof(little));
        memcpy(slotMapping, littleMapping, sizeof(littleMapping));
        return 1;
    }
    memcpy(controllerData, legacy, sizeof(legacy));
    memcpy(slotMapping, legacyMapping, sizeof(legacyMapping));
    return 1;
}
