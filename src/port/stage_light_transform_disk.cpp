// .bsp stage-light transform decoder, see port/stage_light_transform_disk.h.

#include "port/stage_light_transform_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

extern "C" int StageLightTransformDisk_Decode(void *destination, size_t destinationSize,
                                              const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    size_t offset;

    if (destination == NULL || file == NULL || size < STAGE_LIGHT_TRANSFORM_DISK_SIZE ||
            destinationSize < STAGE_LIGHT_TRANSFORM_DISK_SIZE)
        return 0;
    for (offset = 0; offset < STAGE_LIGHT_TRANSFORM_DISK_SIZE; offset += 4) {
        uint32_t value = ReadLE32(src + offset);
        memcpy(dst + offset, &value, sizeof(value));
    }
    return 1;
}
