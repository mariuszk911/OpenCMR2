// .cin car-info decoder: directory bounds and every consumed section.
#include "port/car_info_disk.h"
#include "port/endian.h"

#include <stdio.h>
#include <string.h>

static void PutLE16(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 8) & 0xff);
    p[1] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
#endif
}

static void PutLE32(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

static unsigned int GetHost16(const unsigned char *p)
{
    unsigned short value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static unsigned int GetHostByte(const unsigned char *p)
{
    return p[0];
}

static int Fail(const char *what, int line)
{
    printf("car_info: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static const unsigned int kOffsets[6] = { 12, 0x60, 0x84, 0xd8, 0x128, 0x14c };
static const unsigned int kFileSize = 0x17c;

static void PutVector(unsigned char *p, unsigned int x, unsigned int y, unsigned int z)
{
    PutLE32(p + 0, x);
    PutLE32(p + 4, y);
    PutLE32(p + 8, z);
}

static void PutAngles(unsigned char *p, unsigned int x, unsigned int y, unsigned int z, unsigned int pad)
{
    PutLE16(p + 0, x);
    PutLE16(p + 2, y);
    PutLE16(p + 4, z);
    PutLE16(p + 6, pad);
}

static void BuildSynthetic(unsigned char *file)
{
    unsigned char *light;
    unsigned char *contact;
    unsigned char *glass;
    unsigned char *flexible;
    unsigned char *camera;
    unsigned char *interior;
    int i;

    memset(file, 0xcc, kFileSize);
    for (i = 0; i < 6; i++)
        PutLE16(file + i * 2, kOffsets[i]);

    light = file + kOffsets[0];
    PutLE32(light, 2);
    for (i = 0; i < 2; i++) {
        unsigned char *p = light + 4 + i * 0x28;
        PutVector(p + 0x00, 0x100 + i, 0x200 + i, 0x300 + i);
        PutVector(p + 0x0c, 0x400 + i, 0x500 + i, 0x600 + i);
        PutLE32(p + 0x18, 0x700 + i);
        PutLE32(p + 0x1c, 0x800 + i);
        p[0x20] = (unsigned char)(7 + i);
        p[0x21] = (unsigned char)(9 + i);
        p[0x22] = (unsigned char)(11 + i);
        p[0x23] = (unsigned char)(13 + i);
        PutLE16(p + 0x24, 0x900 + i);
        PutLE16(p + 0x26, 0xa00 + i);
    }

    contact = file + kOffsets[1];
    contact[0] = 3;
    contact[1] = 1;
    contact[2] = 2;
    contact[3] = 4;
    PutLE32(contact + 4, 0x1111);
    PutLE32(contact + 8, 0x2222);
    for (i = 0; i < 3; i++) {
        PutLE32(contact + 12 + i * 8, 0x1000 + i);
        PutLE32(contact + 16 + i * 8, 0x2000 + i);
    }

    glass = file + kOffsets[2];
    for (i = 0; i < 12; i++)
        glass[i] = (unsigned char)(0xa0 + i);
    for (i = 0; i < 6; i++)
        PutVector(glass + 12 + i * 12, 0x10000 + i, 0x20000 + i, 0x30000 + i);

    flexible = file + kOffsets[3];
    PutVector(flexible, 0x11, 0x22, 0x33);
    PutLE32(flexible + 12, 2);
    for (i = 0; i < 2; i++) {
        unsigned char *p = flexible + 16 + i * 32;
        PutVector(p + 0x00, 0x100 + i, 0x200 + i, 0x300 + i);
        PutVector(p + 0x0c, 0x400 + i, 0x500 + i, 0x600 + i);
        PutLE32(p + 0x18, 0x700 + i);
        p[0x1c] = (unsigned char)(0x20 + i);
        p[0x1d] = (unsigned char)(0x21 + i);
        p[0x1e] = (unsigned char)(0x22 + i);
        p[0x1f] = (unsigned char)(0x23 + i);
    }

    camera = file + kOffsets[4];
    for (i = 0; i < 3; i++)
        PutVector(camera + i * 12, 0x1000 + i, 0x2000 + i, 0x3000 + i);

    interior = file + kOffsets[5];
    PutAngles(interior + 0x00, 1, 2, 3, 4);
    PutLE16(interior + 0x08, 0x1005);
    PutLE16(interior + 0x0a, 0x1006);
    PutVector(interior + 0x0c, 0x2001, 0x2002, 0x2003);
    PutLE16(interior + 0x18, 0x1007);
    PutLE16(interior + 0x1a, 0x1008);
    PutAngles(interior + 0x1c, 5, 6, 7, 8);
    PutAngles(interior + 0x24, 9, 10, 11, 12);
    PutLE16(interior + 0x2c, 0x1009);
}

static int CheckSynthetic(const unsigned char *file)
{
    const unsigned char *light = file + kOffsets[0];
    const unsigned char *contact = file + kOffsets[1];
    const unsigned char *glass = file + kOffsets[2];
    const unsigned char *flexible = file + kOffsets[3];
    const unsigned char *camera = file + kOffsets[4];
    const unsigned char *interior = file + kOffsets[5];
    int i;

    for (i = 0; i < 6; i++)
        CHECK(GetHost16(file + i * 2) == kOffsets[i]);

    CHECK(GetHost32(light) == 2);
    for (i = 0; i < 2; i++) {
        const unsigned char *p = light + 4 + i * 0x28;
        CHECK(GetHost32(p + 0x00) == 0x100u + (unsigned)i);
        CHECK(GetHost32(p + 0x04) == 0x200u + (unsigned)i);
        CHECK(GetHost32(p + 0x08) == 0x300u + (unsigned)i);
        CHECK(GetHost32(p + 0x0c) == 0x400u + (unsigned)i);
        CHECK(GetHost32(p + 0x10) == 0x500u + (unsigned)i);
        CHECK(GetHost32(p + 0x14) == 0x600u + (unsigned)i);
        CHECK(GetHost32(p + 0x18) == 0x700u + (unsigned)i);
        CHECK(GetHost32(p + 0x1c) == 0x800u + (unsigned)i);
        CHECK(GetHostByte(p + 0x20) == (unsigned)(7 + i));
        CHECK(GetHostByte(p + 0x21) == (unsigned)(9 + i));
        CHECK(GetHostByte(p + 0x22) == (unsigned)(11 + i));
        CHECK(GetHostByte(p + 0x23) == (unsigned)(13 + i));
        CHECK(GetHost16(p + 0x24) == 0x900u + (unsigned)i);
        CHECK(GetHost16(p + 0x26) == 0xa00u + (unsigned)i);
    }

    CHECK(GetHostByte(contact + 0) == 3);
    CHECK(GetHostByte(contact + 1) == 1);
    CHECK(GetHostByte(contact + 2) == 2);
    CHECK(GetHostByte(contact + 3) == 4);
    CHECK(GetHost32(contact + 4) == 0x1111);
    CHECK(GetHost32(contact + 8) == 0x2222);
    for (i = 0; i < 3; i++) {
        CHECK(GetHost32(contact + 12 + i * 8) == 0x1000u + (unsigned)i);
        CHECK(GetHost32(contact + 16 + i * 8) == 0x2000u + (unsigned)i);
    }

    for (i = 0; i < 12; i++)
        CHECK(GetHostByte(glass + i) == (unsigned)(0xa0 + i));
    for (i = 0; i < 6; i++) {
        CHECK(GetHost32(glass + 12 + i * 12) == 0x10000u + (unsigned)i);
        CHECK(GetHost32(glass + 16 + i * 12) == 0x20000u + (unsigned)i);
        CHECK(GetHost32(glass + 20 + i * 12) == 0x30000u + (unsigned)i);
    }

    CHECK(GetHost32(flexible + 0) == 0x11);
    CHECK(GetHost32(flexible + 4) == 0x22);
    CHECK(GetHost32(flexible + 8) == 0x33);
    CHECK(GetHost32(flexible + 12) == 2);
    for (i = 0; i < 2; i++) {
        const unsigned char *p = flexible + 16 + i * 32;
        CHECK(GetHost32(p + 0x00) == 0x100u + (unsigned)i);
        CHECK(GetHost32(p + 0x04) == 0x200u + (unsigned)i);
        CHECK(GetHost32(p + 0x08) == 0x300u + (unsigned)i);
        CHECK(GetHost32(p + 0x0c) == 0x400u + (unsigned)i);
        CHECK(GetHost32(p + 0x10) == 0x500u + (unsigned)i);
        CHECK(GetHost32(p + 0x14) == 0x600u + (unsigned)i);
        CHECK(GetHost32(p + 0x18) == 0x700u + (unsigned)i);
        CHECK(GetHostByte(p + 0x1c) == (unsigned)(0x20 + i));
        CHECK(GetHostByte(p + 0x1d) == (unsigned)(0x21 + i));
        CHECK(GetHostByte(p + 0x1e) == (unsigned)(0x22 + i));
        CHECK(GetHostByte(p + 0x1f) == (unsigned)(0x23 + i));
    }

    for (i = 0; i < 3; i++) {
        CHECK(GetHost32(camera + i * 12) == 0x1000u + (unsigned)i);
        CHECK(GetHost32(camera + 4 + i * 12) == 0x2000u + (unsigned)i);
        CHECK(GetHost32(camera + 8 + i * 12) == 0x3000u + (unsigned)i);
    }

    CHECK(GetHost16(interior + 0x00) == 1);
    CHECK(GetHost16(interior + 0x02) == 2);
    CHECK(GetHost16(interior + 0x04) == 3);
    CHECK(GetHost16(interior + 0x06) == 4);
    CHECK(GetHost16(interior + 0x08) == 0x1005);
    CHECK(GetHost16(interior + 0x0a) == 0x1006);
    CHECK(GetHost32(interior + 0x0c) == 0x2001);
    CHECK(GetHost32(interior + 0x10) == 0x2002);
    CHECK(GetHost32(interior + 0x14) == 0x2003);
    CHECK(GetHost16(interior + 0x18) == 0x1007);
    CHECK(GetHost16(interior + 0x1a) == 0x1008);
    CHECK(GetHost16(interior + 0x1c) == 5);
    CHECK(GetHost16(interior + 0x1e) == 6);
    CHECK(GetHost16(interior + 0x20) == 7);
    CHECK(GetHost16(interior + 0x22) == 8);
    CHECK(GetHost16(interior + 0x24) == 9);
    CHECK(GetHost16(interior + 0x26) == 10);
    CHECK(GetHost16(interior + 0x28) == 11);
    CHECK(GetHost16(interior + 0x2c) == 0x1009);
    return 0;
}

static int CheckCaptured(const unsigned char *file, unsigned int size)
{
    unsigned int offsets[6];
    int i;

    CHECK(size >= 12);
    for (i = 0; i < 6; i++)
        offsets[i] = GetHost16(file + i * 2);
    CHECK(offsets[0] >= 12);
    for (i = 0; i < 5; i++)
        CHECK(offsets[i] <= offsets[i + 1]);
    CHECK(offsets[5] <= size);
    CHECK(GetHost32(file + offsets[0]) > 0);
    CHECK(GetHostByte(file + offsets[1]) > 0);
    CHECK((offsets[5] - offsets[4]) % 12 == 0);
    CHECK(offsets[5] - offsets[4] >= 12);
    CHECK(size - offsets[5] >= 0x2e);
    return 0;
}

static int LoadFile(const char *path, unsigned char *buffer, unsigned int capacity, unsigned int *outSize)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    if (f == NULL)
        return 0;
    got = fread(buffer, 1, capacity, f);
    fclose(f);
    *outSize = (unsigned int)got;
    return got > 0;
}

int main(int argc, char **argv)
{
    unsigned char file[kFileSize];
    unsigned char shifted[kFileSize + 1];
    unsigned char output[0x10000];
    unsigned char inplace[kFileSize];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;
    int i;

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    // The caller decodes the .cin buffer in place.
    memcpy(inplace, file, sizeof(file));
    CHECK(CarInfoDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace)) != 0);
    if (CheckSynthetic(inplace))
        return 1;

    // Unaligned source.
    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(output, 0, sizeof(output));
    CHECK(CarInfoDisk_Decode(output, sizeof(output), shifted + 1, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    // A section count that would walk past its section.
    BuildSynthetic(file);
    PutLE32(file + kOffsets[0], 3);
    memcpy(inplace, file, sizeof(file));
    CHECK(CarInfoDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace)) == 0);
    CHECK(memcmp(inplace, file, sizeof(file)) == 0);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    PutLE32(file + kOffsets[3] + 12, 3);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);

    // Non-monotonic offsets, an offset past the file, a short file and a
    // section shorter than its header.
    BuildSynthetic(file);
    PutLE16(file + 4, kOffsets[0] - 1);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    PutLE16(file + 10, kFileSize + 1);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, 11) == 0);
    BuildSynthetic(file);
    PutLE16(file + 6, kOffsets[2] + 1);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);

    CHECK(CarInfoDisk_Decode(NULL, sizeof(output), file, sizeof(file)) == 0);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), NULL, sizeof(file)) == 0);
    CHECK(CarInfoDisk_Decode(output, sizeof(output) - 1, file, sizeof(file)) == 0);

    if (argc > 1) {
        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        memset(output, 0, sizeof(output));
        CHECK(CarInfoDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        if (CheckCaptured(output, capturedSize))
            return 1;
    }

    printf("car_info: OK\n");
    return 0;
}
