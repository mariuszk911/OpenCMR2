// .csp stage start table decoder, see port/start_table_disk.h.

#include "port/start_table_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

namespace {

void StoreHost16(void *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

void StoreHost32(void *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

void Decode16(unsigned char *dst, const unsigned char *src)
{
    StoreHost16(dst, ReadLE16(src));
}

void Decode32(unsigned char *dst, const unsigned char *src)
{
    StoreHost32(dst, ReadLE32(src));
}

// Start-point n: 0 at 0x00, 1 at 0x0c, then 2.. at 0x20 + (n - 2) * 12.
size_t StartPointOffset(unsigned int n)
{
    if (n == 0)
        return 0;
    if (n == 1)
        return 0x0c;
    return 0x20 + (size_t)(n - 2) * 12;
}

unsigned int CountStartPoints(size_t size)
{
    unsigned int count = 0;

    if (size >= 0x0c)
        count = 1;
    if (size >= 0x18)
        count = 2;
    while (count < 6 && StartPointOffset(count) + 12 <= size)
        count++;
    return count;
}

} // namespace

extern "C" int StartTableDisk_Decode(void *destination, size_t destinationSize,
                                     const void *file, size_t fileSize,
                                     unsigned int *outStartCount)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    unsigned int count;
    size_t offset;

    if (destination == NULL || file == NULL || fileSize < START_TABLE_MIN_SIZE ||
            destinationSize < fileSize)
        return 0;

    count = CountStartPoints(fileSize);
    memmove(dst, src, fileSize);
    for (offset = 0; offset + 4 <= 0x18; offset += 4)
        Decode32(dst + offset, src + offset);
    for (offset = 0x18; offset + 2 <= 0x20; offset += 2)
        Decode16(dst + offset, src + offset);
    for (offset = 0x20; offset + 4 <= fileSize; offset += 4)
        Decode32(dst + offset, src + offset);

    if (outStartCount != NULL)
        *outStartCount = count;
    return 1;
}
