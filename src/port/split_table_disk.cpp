// .tm? team split-table decoder, see port/split_table_disk.h.

#include "port/split_table_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

extern "C" int SplitTableDisk_Decode(void *destination, size_t destinationSize,
                                     const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    size_t offset;

    if (destination == NULL || file == NULL || size < SPLIT_TABLE_BYTE_PREFIX ||
            destinationSize < size)
        return 0;
    if ((size - SPLIT_TABLE_BYTE_PREFIX) % 4 != 0)
        return 0;

    memmove(dst, src, size);
    for (offset = SPLIT_TABLE_BYTE_PREFIX; offset < size; offset += 4) {
        uint32_t value = ReadLE32(src + offset);
        memcpy(dst + offset, &value, sizeof(value));
    }
    return 1;
}
