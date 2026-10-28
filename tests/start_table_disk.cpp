// .csp start table: the six start-point ints, the FixAngles and the tail.
#include "port/endian.h"
#include "port/start_table_disk.h"

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
    printf("start_table: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static const unsigned int kFileSize = 0x50;
static const unsigned int kCapturedTail[12] = {
    458752u, 10944512u, 8291387u, 0u, 20907914u, 8291387u,
    0u, 20907914u, 0u, 10944512u, 0u, 10944512u
};

static void BuildSynthetic(unsigned char *file)
{
    int i;

    memset(file, 0xcc, kFileSize);
    for (i = 0; i < 6; i++)
        PutLE32(file + (size_t)i * 4, 0x1000u + (unsigned)i);
    for (i = 0; i < 4; i++)
        PutLE16(file + 0x18 + (size_t)i * 2, 0x2000u + (unsigned)i);
    for (i = 0; i < 12; i++)
        PutLE32(file + 0x20 + (size_t)i * 4, 0x3000u + (unsigned)i);
}

static int CheckSynthetic(const unsigned char *file)
{
    int i;

    for (i = 0; i < 6; i++)
        CHECK(GetHost32(file + (size_t)i * 4) == 0x1000u + (unsigned)i);
    for (i = 0; i < 4; i++)
        CHECK(GetHost16(file + 0x18 + (size_t)i * 2) == 0x2000u + (unsigned)i);
    for (i = 0; i < 12; i++)
        CHECK(GetHost32(file + 0x20 + (size_t)i * 4) == 0x3000u + (unsigned)i);
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
    unsigned int count = 0;

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(StartTableDisk_Decode(output, sizeof(output), file, sizeof(file), &count) != 0);
    CHECK(count == 6);
    if (CheckSynthetic(output))
        return 1;

    memcpy(inplace, file, sizeof(file));
    CHECK(StartTableDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace), NULL) != 0);
    if (CheckSynthetic(inplace))
        return 1;

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(output, 0, sizeof(output));
    CHECK(StartTableDisk_Decode(output, sizeof(output), shifted + 1, sizeof(file), NULL) != 0);
    if (CheckSynthetic(output))
        return 1;

    // A table shorter than 0x20 is rejected before the destination changes.
    memset(output, 0x5a, sizeof(output));
    BuildSynthetic(file);
    CHECK(StartTableDisk_Decode(output, sizeof(output), file, START_TABLE_MIN_SIZE - 1, NULL) == 0);
    CHECK(output[0] == 0x5a && output[1] == 0x5a);
    CHECK(StartTableDisk_Decode(output, START_TABLE_MIN_SIZE - 1, file, sizeof(file), NULL) == 0);
    CHECK(StartTableDisk_Decode(NULL, sizeof(output), file, sizeof(file), NULL) == 0);
    CHECK(StartTableDisk_Decode(output, sizeof(output), NULL, sizeof(file), NULL) == 0);

    if (argc > 1) {
        int i;

        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        CHECK(capturedSize >= START_TABLE_MIN_SIZE);
        memset(output, 0, sizeof(output));
        CHECK(StartTableDisk_Decode(output, capturedSize, captured, capturedSize, &count) != 0);
        CHECK(count == 6);
        CHECK(GetHost32(output + 0x00) == 7929198u);
        CHECK(GetHost32(output + 0x0c) == 7918139u);
        CHECK(GetHost16(output + 0x18) == 0);
        CHECK(GetHost16(output + 0x1a) == 4092u);
        for (i = 0; i < 12; i++)
            CHECK(GetHost32(output + 0x20 + (size_t)i * 4) == kCapturedTail[i]);
    }

    printf("start_table: OK\n");
    return 0;
}
