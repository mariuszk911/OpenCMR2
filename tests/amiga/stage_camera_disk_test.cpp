// Real 68060 coverage for the .tsc trackside-camera decoder.
#include "port/endian.h"
#include "port/stage_camera_disk.h"

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
    printf("stage_camera: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static const unsigned int kCount = 2;
static const unsigned int kFileSize = 4 + kCount * STAGE_CAMERA_RECORD_SIZE;

static void PutVector(unsigned char *p, unsigned int x, unsigned int y, unsigned int z)
{
    PutLE32(p + 0, x);
    PutLE32(p + 4, y);
    PutLE32(p + 8, z);
}

static void BuildSynthetic(unsigned char *file)
{
    unsigned int i;

    memset(file, 0, kFileSize);
    PutLE32(file, kCount);
    for (i = 0; i < kCount; i++) {
        unsigned char *p = file + 4 + i * STAGE_CAMERA_RECORD_SIZE;
        int k;

        PutLE16(p + 0x00, 0x100 + i);
        PutLE16(p + 0x02, 0x200 + i);
        for (k = 0; k < 5; k++)
            PutVector(p + 0x04 + k * 12, 0x1000 + i * 0x100 + k, 0x2000 + i * 0x100 + k, 0x3000 + i * 0x100 + k);
        for (k = 0; k < 10; k++)
            PutLE32(p + 0x40 + k * 4, 0x4000 + i * 0x100 + k);
    }
}

static int CheckSynthetic(const unsigned char *file)
{
    unsigned int i;

    CHECK(GetHost32(file) == kCount);
    for (i = 0; i < kCount; i++) {
        const unsigned char *p = file + 4 + i * STAGE_CAMERA_RECORD_SIZE;
        int k;

        CHECK(GetHost16(p + 0x00) == 0x100u + i);
        CHECK(GetHost16(p + 0x02) == 0x200u + i);
        for (k = 0; k < 5; k++)
            CHECK(GetHost32(p + 0x04 + k * 12 + 8) == 0x3000u + i * 0x100u + (unsigned)k);
        CHECK(GetHost32(p + 0x40) == 0x4000u + i * 0x100u);
        CHECK(GetHost32(p + 0x64) == 0x4009u + i * 0x100u);
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char file[kFileSize];
    unsigned char output[0x10000];
    unsigned char inplace[kFileSize];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;
    uint32_t count = 0;

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(StageCameraDisk_Decode(output, sizeof(output), file, sizeof(file), &count) != 0);
    CHECK(count == kCount);
    if (CheckSynthetic(output))
        return 1;

    memcpy(inplace, file, sizeof(file));
    count = 0;
    CHECK(StageCameraDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace), &count) != 0);
    if (CheckSynthetic(inplace))
        return 1;

    BuildSynthetic(file);
    PutLE32(file, 0x40000000u);
    CHECK(StageCameraDisk_Decode(output, sizeof(output), file, sizeof(file), &count) == 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got > 4);
        capturedSize = (unsigned int)got;
        memset(output, 0, sizeof(output));
        CHECK(StageCameraDisk_Decode(output, capturedSize, captured, capturedSize, &count) != 0);
        CHECK(count == 21);
        CHECK(GetHost16(output + 4) == (unsigned short)-1792);
        CHECK(GetHost32(output + 4 + 0x04) == (unsigned)-36280);
        CHECK(GetHost32(output + 4 + 0x40) == 4587520u);
    }

    printf("stage_camera: OK\n");
    return 0;
}
