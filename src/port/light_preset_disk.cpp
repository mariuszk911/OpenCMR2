// .hor light-preset decoder, see port/light_preset_disk.h.

#include "port/light_preset_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

extern "C" int LightPresetDisk_Decode(void *destination, size_t destinationSize,
                                      const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    size_t offset;

    if (destination == NULL || file == NULL || size < LIGHT_PRESET_DISK_SIZE ||
            destinationSize < LIGHT_PRESET_DISK_SIZE)
        return 0;

    memcpy(dst, src, LIGHT_PRESET_DISK_SIZE);
    for (offset = 0; offset < 0x1c; offset += 4) {
        uint32_t value = ReadLE32(src + offset);
        memcpy(dst + offset, &value, sizeof(value));
    }
    return 1;
}
