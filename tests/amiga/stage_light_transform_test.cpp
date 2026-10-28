// Real 68060 coverage for the .bsp stage-light transform decoder.
#include "port/endian.h"
#include "port/stage_light_transform_disk.h"

#include <stdio.h>
#include <string.h>

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

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("stage_light_transform: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void BuildSynthetic(unsigned char *file)
{
    int i;

    for (i = 0; i < 12; i++)
        PutLE32(file + (size_t)i * 4, 0x1000u + (unsigned)i);
}

static int CheckSynthetic(const unsigned char *file)
{
    int i;

    for (i = 0; i < 12; i++)
        CHECK(GetHost32(file + (size_t)i * 4) == 0x1000u + (unsigned)i);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char file[STAGE_LIGHT_TRANSFORM_DISK_SIZE];
    unsigned char output[0x10000];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(StageLightTransformDisk_Decode(output, sizeof(output), file, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    CHECK(StageLightTransformDisk_Decode(output, sizeof(output), file,
                                         STAGE_LIGHT_TRANSFORM_DISK_SIZE - 1) == 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got >= STAGE_LIGHT_TRANSFORM_DISK_SIZE);
        capturedSize = (unsigned int)got;
        memset(output, 0, sizeof(output));
        CHECK(StageLightTransformDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        CHECK(GetHost32(output + 0x00) == (unsigned)-1143);
        CHECK(GetHost32(output + 0x08) == (unsigned)-65526);
        CHECK(GetHost32(output + 0x10) == 65536u);
        CHECK(GetHost32(output + 0x24) == 8324042u);
        CHECK(GetHost32(output + 0x2c) == 20418576u);
    }

    printf("stage_light_transform: OK\n");
    return 0;
}
