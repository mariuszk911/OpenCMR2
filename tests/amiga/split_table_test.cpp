// Real 68060 coverage for the .tm? split-table decoder.
#include "port/endian.h"
#include "port/split_table_disk.h"

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
    printf("split_table: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static const unsigned int kFileSize = SPLIT_TABLE_BYTE_PREFIX + 16;

static void BuildSynthetic(unsigned char *file)
{
    unsigned int i;

    for (i = 0; i < SPLIT_TABLE_BYTE_PREFIX; i++)
        file[i] = (unsigned char)(0x20 + (i & 0x3f));
    for (i = 0; i < 4; i++)
        PutLE32(file + SPLIT_TABLE_BYTE_PREFIX + i * 4, 0x1000u + i);
}

static int CheckSynthetic(const unsigned char *file)
{
    unsigned int i;

    for (i = 0; i < SPLIT_TABLE_BYTE_PREFIX; i++)
        CHECK(file[i] == (unsigned char)(0x20 + (i & 0x3f)));
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(file + SPLIT_TABLE_BYTE_PREFIX + i * 4) == 0x1000u + i);
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
    CHECK(SplitTableDisk_Decode(output, sizeof(output), file, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    memcpy(inplace, file, sizeof(file));
    CHECK(SplitTableDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace)) != 0);
    if (CheckSynthetic(inplace))
        return 1;

    CHECK(SplitTableDisk_Decode(output, sizeof(output), file,
                                SPLIT_TABLE_BYTE_PREFIX - 1) == 0);
    CHECK(SplitTableDisk_Decode(output, sizeof(output), file, sizeof(file) - 1) == 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got >= SPLIT_TABLE_BYTE_PREFIX + 4);
        capturedSize = (unsigned int)got;
        memset(output, 0, sizeof(output));
        CHECK(SplitTableDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        CHECK(output[0] == 0x78);
        CHECK(output[1] == 0x00);
        CHECK(output[2] == 0xf6);
        CHECK(output[3] == 0x00);
        CHECK(output[4] == 0xfe);
        CHECK(output[0x4f] == 0x00);
        CHECK(GetHost32(output + 0x90) == 0x395c28u);
        CHECK(GetHost32(output + 0x2d0) == 0x716666u);
        CHECK(GetHost32(output + 0x310) == 0x725c28u);
        CHECK(GetHost32(output + 0x34c) == 0x1900000u);
    }

    printf("split_table: OK\n");
    return 0;
}
