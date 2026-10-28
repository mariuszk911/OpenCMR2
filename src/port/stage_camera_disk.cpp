// .tsc trackside-camera decoder, see port/stage_camera_disk.h.

#include "port/stage_camera_disk.h"

#include "port/endian.h"

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

void DecodeVector(unsigned char *dst, const unsigned char *src)
{
    Decode32(dst + 0, src + 0);
    Decode32(dst + 4, src + 4);
    Decode32(dst + 8, src + 8);
}

void DecodeRecord(unsigned char *dst, const unsigned char *src)
{
    int i;

    Decode16(dst + 0x00, src + 0x00);
    Decode16(dst + 0x02, src + 0x02);
    for (i = 0; i < 5; i++)
        DecodeVector(dst + 0x04 + (size_t)i * 0x0c, src + 0x04 + (size_t)i * 0x0c);
    for (i = 0; i < 11; i++)
        Decode32(dst + 0x40 + (size_t)i * 4, src + 0x40 + (size_t)i * 4);
}

} // namespace

extern "C" int StageCameraDisk_Decode(void *destination, size_t destinationSize,
                                      const void *file, size_t fileSize,
                                      uint32_t *outCount)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    uint32_t count;
    size_t i;

    if (destination == NULL || file == NULL || fileSize < 4 || destinationSize < fileSize)
        return 0;
    count = ReadLE32(src);
    if (count > (fileSize - 4) / STAGE_CAMERA_RECORD_SIZE)
        return 0;

    memmove(dst, src, fileSize);
    StoreHost32(dst, count);
    for (i = 0; i < (size_t)count; i++)
        DecodeRecord(dst + 4 + i * STAGE_CAMERA_RECORD_SIZE, src + 4 + i * STAGE_CAMERA_RECORD_SIZE);
    if (outCount != NULL)
        *outCount = count;
    return 1;
}
