// .cod co-driver calls: 32-bit little-endian records.
#include "port/cod_disk.h"
#include "port/endian.h"

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
    printf("cod_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

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
    unsigned char file[16];
    unsigned char shifted[17];
    unsigned char output[0x10000];
    unsigned char inplace[16];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;
    unsigned int count = 0;
    int i;

    for (i = 0; i < 4; i++)
        PutLE32(file + i * 4, 0x1000u + i);
    memset(output, 0, sizeof(output));
    CHECK(CodDisk_Decode(output, sizeof(output), file, sizeof(file), &count) != 0);
    CHECK(count == 4);
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(output + i * 4) == 0x1000u + i);

    memcpy(inplace, file, sizeof(file));
    count = 0;
    CHECK(CodDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace), &count) != 0);
    CHECK(count == 4);
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(inplace + i * 4) == 0x1000u + i);

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(output, 0, sizeof(output));
    CHECK(CodDisk_Decode(output, sizeof(output), shifted + 1, sizeof(file), NULL) != 0);
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(output + i * 4) == 0x1000u + i);

    memset(output, 0x5a, sizeof(output));
    CHECK(CodDisk_Decode(output, sizeof(output), file, sizeof(file) - 1, &count) == 0);
    CHECK(output[0] == 0x5a);
    CHECK(CodDisk_Decode(output, 3, file, sizeof(file), &count) == 0);
    CHECK(CodDisk_Decode(NULL, sizeof(output), file, sizeof(file), &count) == 0);
    CHECK(CodDisk_Decode(output, sizeof(output), NULL, sizeof(file), &count) == 0);

    if (argc > 1) {
        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        CHECK(capturedSize == 33u * 4);
        memset(output, 0, sizeof(output));
        CHECK(CodDisk_Decode(output, capturedSize, captured, capturedSize, &count) != 0);
        CHECK(count == 33);
        CHECK(GetHost32(output + 0) == 339);
        CHECK(GetHost32(output + 4) == 1188);
        CHECK(GetHost32(output + 8) == 83);
        CHECK(GetHost32(output + 32 * 4) == 228);
    }

    printf("cod_disk: OK\n");
    return 0;
}
