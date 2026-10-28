// Real 68060 coverage for the .csp start-table decoder.
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

static void BuildSynthetic(unsigned char *file)
{
    int i;

    memset(file, 0, kFileSize);
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

int main(int argc, char **argv)
{
    unsigned char file[kFileSize];
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

    BuildSynthetic(file);
    CHECK(StartTableDisk_Decode(output, sizeof(output), file, START_TABLE_MIN_SIZE - 1, NULL) == 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got >= START_TABLE_MIN_SIZE);
        capturedSize = (unsigned int)got;
        memset(output, 0, sizeof(output));
        CHECK(StartTableDisk_Decode(output, capturedSize, captured, capturedSize, &count) != 0);
        CHECK(count == 6);
        CHECK(GetHost32(output + 0x00) == 7929198u);
        CHECK(GetHost32(output + 0x04) == 0);
        CHECK(GetHost32(output + 0x08) == 20915184u);
        CHECK(GetHost32(output + 0x0c) == 7918139u);
        CHECK(GetHost16(output + 0x18) == 0);
        CHECK(GetHost16(output + 0x1a) == 4092u);
        CHECK(GetHost16(output + 0x1c) == 0);
        CHECK(GetHost16(output + 0x1e) == 167u);
        CHECK(GetHost32(output + 0x20) == 458752u);
        CHECK(GetHost32(output + 0x24) == 10944512u);
    }

    printf("start_table: OK\n");
    return 0;
}
