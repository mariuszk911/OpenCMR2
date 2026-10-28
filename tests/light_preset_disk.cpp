// .hor light presets: seven little-endian ints, byte colour tail.
#include "port/endian.h"
#include "port/light_preset_disk.h"

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
    printf("light_preset: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void BuildSynthetic(unsigned char *file)
{
    unsigned int i;

    memset(file, 0xcc, 0x50);
    for (i = 0; i < 7; i++)
        PutLE32(file + i * 4, 0x1000u + i);
    for (i = 0x1c; i < 0x50; i++)
        file[i] = (unsigned char)(0x40 + i);
}

static int CheckSynthetic(const unsigned char *file)
{
    unsigned int i;

    for (i = 0; i < 7; i++)
        CHECK(GetHost32(file + i * 4) == 0x1000u + i);
    for (i = 0x1c; i < LIGHT_PRESET_DISK_SIZE; i++)
        CHECK(file[i] == (unsigned char)(0x40 + i));
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
    unsigned char file[0x50];
    unsigned char shifted[0x51];
    unsigned char output[0x10000];
    unsigned char captured[0x10000];
    unsigned int capturedSize = 0;

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(LightPresetDisk_Decode(output, sizeof(output), file, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(output, 0, sizeof(output));
    CHECK(LightPresetDisk_Decode(output, sizeof(output), shifted + 1, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    // A short record or destination is rejected before the output changes.
    BuildSynthetic(file);
    memset(output, 0x5a, sizeof(output));
    CHECK(LightPresetDisk_Decode(output, sizeof(output), file, LIGHT_PRESET_DISK_SIZE - 1) == 0);
    CHECK(output[0] == 0x5a && output[1] == 0x5a);
    CHECK(LightPresetDisk_Decode(output, LIGHT_PRESET_DISK_SIZE - 1, file, sizeof(file)) == 0);
    CHECK(LightPresetDisk_Decode(NULL, sizeof(output), file, sizeof(file)) == 0);
    CHECK(LightPresetDisk_Decode(output, sizeof(output), NULL, sizeof(file)) == 0);

    if (argc > 1) {
        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        CHECK(capturedSize >= LIGHT_PRESET_DISK_SIZE);
        memset(output, 0, sizeof(output));
        CHECK(LightPresetDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        CHECK(GetHost32(output + 0x00) == (unsigned)-2031616);
        CHECK(GetHost32(output + 0x04) == (unsigned)-2359296);
        CHECK(GetHost32(output + 0x08) == (unsigned)-720896);
        CHECK(GetHost32(output + 0x0c) == 65536u);
        CHECK(GetHost32(output + 0x10) == 6225920u);
        CHECK(GetHost32(output + 0x14) == 0);
        CHECK(GetHost32(output + 0x18) == 0);
        CHECK(output[0x1c] == 0x17);
        CHECK(output[0x1d] == 0x54);
        CHECK(output[LIGHT_PRESET_DISK_SIZE - 1] == 0x80);
    }

    printf("light_preset: OK\n");
    return 0;
}
