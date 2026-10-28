// .cin car-info decoder, see port/car_info_disk.h.

#include "port/car_info_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

namespace {

const size_t kDirectorySize = 12;
const size_t kLightPointSize = 0x28;
const size_t kContactHeaderSize = 12;
const size_t kContactPointSize = 8;
const size_t kGlassVerticesOffset = 12;
const size_t kGlassVertexSize = 12;
const size_t kGlassVertexCount = 6;
const size_t kFlexibleLineHeaderSize = 16;
const size_t kFlexibleLineDescriptorSize = 32;
const size_t kCameraOffsetSize = 12;
const size_t kInteriorMinSize = 0x2e;

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

void DecodeAngles(unsigned char *dst, const unsigned char *src)
{
    int i;

    for (i = 0; i < 4; i++)
        Decode16(dst + (size_t)i * 2, src + (size_t)i * 2);
}

int DecodeLights(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t length = end - begin;
    int32_t count;
    size_t i;

    if (length < 4)
        return 0;
    count = (int32_t)ReadLE32(src + begin);
    if (count < 0 || (size_t)count > (length - 4) / kLightPointSize)
        return 0;
    Decode32(dst + begin, src + begin);
    for (i = 0; i < (size_t)count; i++) {
        unsigned char *d = dst + begin + 4 + i * kLightPointSize;
        const unsigned char *s = src + begin + 4 + i * kLightPointSize;

        DecodeVector(d + 0x00, s + 0x00);
        DecodeVector(d + 0x0c, s + 0x0c);
        Decode32(d + 0x18, s + 0x18);
        Decode32(d + 0x1c, s + 0x1c);
        Decode16(d + 0x24, s + 0x24);
        Decode16(d + 0x26, s + 0x26);
    }
    return 1;
}

int DecodeContact(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t length = end - begin;
    unsigned int count;
    size_t i;

    if (length < kContactHeaderSize)
        return 0;
    count = src[begin];
    if ((size_t)count > (length - kContactHeaderSize) / kContactPointSize)
        return 0;
    Decode32(dst + begin + 4, src + begin + 4);
    Decode32(dst + begin + 8, src + begin + 8);
    for (i = 0; i < (size_t)count; i++) {
        unsigned char *d = dst + begin + kContactHeaderSize + i * kContactPointSize;
        const unsigned char *s = src + begin + kContactHeaderSize + i * kContactPointSize;

        Decode32(d + 0, s + 0);
        Decode32(d + 4, s + 4);
    }
    return 1;
}

int DecodeGlass(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t i;
    size_t required = kGlassVerticesOffset + kGlassVertexCount * kGlassVertexSize;

    if (end - begin < required)
        return 0;
    for (i = 0; i < kGlassVertexCount; i++) {
        size_t offset = begin + kGlassVerticesOffset + i * kGlassVertexSize;
        DecodeVector(dst + offset, src + offset);
    }
    return 1;
}

int DecodeFlexibleLines(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t length = end - begin;
    int32_t count;
    size_t i;

    if (length < kFlexibleLineHeaderSize)
        return 0;
    DecodeVector(dst + begin, src + begin);
    count = (int32_t)ReadLE32(src + begin + 12);
    if (count < 0 || (size_t)count > (length - kFlexibleLineHeaderSize) / kFlexibleLineDescriptorSize)
        return 0;
    Decode32(dst + begin + 12, src + begin + 12);
    for (i = 0; i < (size_t)count; i++) {
        unsigned char *d = dst + begin + kFlexibleLineHeaderSize + i * kFlexibleLineDescriptorSize;
        const unsigned char *s = src + begin + kFlexibleLineHeaderSize + i * kFlexibleLineDescriptorSize;

        DecodeVector(d + 0x00, s + 0x00);
        DecodeVector(d + 0x0c, s + 0x0c);
        Decode32(d + 0x18, s + 0x18);
    }
    return 1;
}

int DecodeCameraOffsets(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t length = end - begin;
    size_t i;

    if (length % kCameraOffsetSize != 0)
        return 0;
    for (i = 0; i < length / kCameraOffsetSize; i++) {
        size_t offset = begin + i * kCameraOffsetSize;
        DecodeVector(dst + offset, src + offset);
    }
    return 1;
}

int DecodeInterior(unsigned char *dst, const unsigned char *src, size_t begin, size_t end)
{
    size_t length = end - begin;

    if (length < kInteriorMinSize)
        return 0;
    DecodeAngles(dst + begin + 0x00, src + begin + 0x00);
    Decode16(dst + begin + 0x08, src + begin + 0x08);
    Decode16(dst + begin + 0x0a, src + begin + 0x0a);
    DecodeVector(dst + begin + 0x0c, src + begin + 0x0c);
    Decode16(dst + begin + 0x18, src + begin + 0x18);
    Decode16(dst + begin + 0x1a, src + begin + 0x1a);
    DecodeAngles(dst + begin + 0x1c, src + begin + 0x1c);
    DecodeAngles(dst + begin + 0x24, src + begin + 0x24);
    Decode16(dst + begin + 0x2c, src + begin + 0x2c);
    return 1;
}

// Validates every section before the destination is touched, so a rejected
// file leaves the caller's buffer (or an in-place decode) unchanged.
int ValidateSections(const unsigned char *src, size_t fileSize, const unsigned short *section)
{
    size_t length;
    int32_t count;

    length = (size_t)section[1] - section[0];
    if (length < 4 || (int32_t)ReadLE32(src + section[0]) < 0 ||
            (size_t)(int32_t)ReadLE32(src + section[0]) > (length - 4) / kLightPointSize)
        return 0;

    length = (size_t)section[2] - section[1];
    if (length < kContactHeaderSize ||
            (size_t)src[section[1]] > (length - kContactHeaderSize) / kContactPointSize)
        return 0;

    if ((size_t)section[3] - section[2] < kGlassVerticesOffset + kGlassVertexCount * kGlassVertexSize)
        return 0;

    length = (size_t)section[4] - section[3];
    if (length < kFlexibleLineHeaderSize)
        return 0;
    count = (int32_t)ReadLE32(src + section[3] + 12);
    if (count < 0 || (size_t)count > (length - kFlexibleLineHeaderSize) / kFlexibleLineDescriptorSize)
        return 0;

    if (((size_t)section[5] - section[4]) % kCameraOffsetSize != 0)
        return 0;
    if (fileSize - section[5] < kInteriorMinSize)
        return 0;
    return 1;
}

} // namespace

extern "C" int CarInfoDisk_Decode(void *destination, size_t destinationSize,
                                  const void *file, size_t fileSize)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    unsigned short section[6];
    int i;

    if (destination == NULL || file == NULL || fileSize < kDirectorySize ||
            destinationSize < fileSize)
        return 0;

    for (i = 0; i < 6; i++)
        section[i] = ReadLE16(src + (size_t)i * 2);
    if (section[0] < kDirectorySize)
        return 0;
    for (i = 0; i < 5; i++) {
        if (section[i] > section[i + 1])
            return 0;
    }
    if (section[5] > fileSize)
        return 0;
    if (!ValidateSections(src, fileSize, section))
        return 0;

    memmove(dst, src, fileSize);
    for (i = 0; i < 6; i++) {
        unsigned char *d = dst + (size_t)i * 2;
        StoreHost16(d, section[i]);
    }

    if (!DecodeLights(dst, src, section[0], section[1]) ||
            !DecodeContact(dst, src, section[1], section[2]) ||
            !DecodeGlass(dst, src, section[2], section[3]) ||
            !DecodeFlexibleLines(dst, src, section[3], section[4]) ||
            !DecodeCameraOffsets(dst, src, section[4], section[5]) ||
            !DecodeInterior(dst, src, section[5], fileSize))
        return 0;
    return 1;
}
