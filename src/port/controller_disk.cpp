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
