// Real 68060 coverage for the .cin car-info decoder. The fixture is the
// archive payload the game hands to the decoder (little-endian section data).
#include "port/car_info_disk.h"
#include "port/endian.h"

#include <stdint.h>
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
    unsigned char *light = file + kOffsets[0];
    unsigned char *contact = file + kOffsets[1];
    unsigned char *glass = file + kOffsets[2];
    unsigned char *flexible = file + kOffsets[3];
    unsigned char *camera = file + kOffsets[4];
    unsigned char *interior = file + kOffsets[5];
    int i;

    memset(file, 0xcc, kFileSize);
    for (i = 0; i < 6; i++)
        PutLE16(file + i * 2, kOffsets[i]);

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

    for (i = 0; i < 12; i++)
        glass[i] = (unsigned char)(0xa0 + i);
    for (i = 0; i < 6; i++)
        PutVector(glass + 12 + i * 12, 0x10000 + i, 0x20000 + i, 0x30000 + i);

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

    for (i = 0; i < 3; i++)
        PutVector(camera + i * 12, 0x1000 + i, 0x2000 + i, 0x3000 + i);

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
    const unsigned char *flexible = file + kOffsets[3];
    const unsigned char *camera = file + kOffsets[4];
    const unsigned char *interior = file + kOffsets[5];
    int i;

    for (i = 0; i < 6; i++)
        CHECK(GetHost16(file + i * 2) == kOffsets[i]);
    CHECK(GetHost32(light) == 2);
    CHECK(GetHost32(light + 4) == 0x100);
    CHECK(GetHost32(light + 4 + 0x28 + 0x18) == 0x701);
    CHECK(GetHost16(light + 4 + 0x28 + 0x26) == 0xa01);
    CHECK(light[4 + 0x20] == 7);
    CHECK(contact[0] == 3);
    CHECK(GetHost32(contact + 4) == 0x1111);
    CHECK(GetHost32(contact + 16) == 0x2000);
    CHECK(GetHost32(flexible + 12) == 2);
    CHECK(GetHost32(flexible + 16 + 32 + 0x18) == 0x701);
    CHECK(GetHost32(camera + 2 * 12) == 0x1002);
    CHECK(GetHost16(interior + 0x00) == 1);
    CHECK(GetHost32(interior + 0x0c) == 0x2001);
    CHECK(GetHost16(interior + 0x2c) == 0x1009);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char file[kFileSize];
    unsigned char output[0x10000];
    unsigned char inplace[kFileSize];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;

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

    // A count past the section is rejected.
    BuildSynthetic(file);
    PutLE32(file + kOffsets[0], 3);
    CHECK(CarInfoDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got >= 12);
        capturedSize = (unsigned int)got;
        memset(output, 0, sizeof(output));
        CHECK(CarInfoDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        CHECK(GetHost32(output + GetHost16(output + 0)) > 0);
        CHECK(output[GetHost16(output + 2)] > 0);
    }

    printf("car_info: OK\n");
    return 0;
}
