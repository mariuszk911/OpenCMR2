// Real 68060 coverage for the Controller.rcf decoder: the fixture (or the
// embedded synthetic file) holds little-endian file bytes and is decoded by
// the target. Host coverage is tests/controller_disk.cpp.
#include "Input.h"
#include "port/controller_disk.h"
#include "port/endian.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern "C" void Sys_Log(const char *, ...) {}

// In the forced-swap host build the fixture stands in for the bytes a real
// big-endian memcpy would see; the real target reads the same little-endian
// file bytes directly.
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

static int Fail(const char *what, int line)
{
    printf("controller_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void FillSynthetic(unsigned char *file)
{
    int i;

    memset(file, 0, CONTROLLER_DISK_FILE_SIZE);
    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        unsigned char *p = file + (size_t)i * sizeof(ControllerData);
        PutLE32(p + offsetof(ControllerData, field_0x0), 0x11000000u + (unsigned)i);
        PutLE32(p + offsetof(ControllerData, field_0x4), 0x22000000u + (unsigned)i);
        PutLE32(p + offsetof(ControllerData, field_0x10c), 0x33110000u + (unsigned)i);
        PutLE16(p + offsetof(ControllerData, field_0x128), 0x10u + (unsigned)i);
        PutLE16(p + offsetof(ControllerData, field_0x13c), 0x20u + (unsigned)i);
        p[offsetof(ControllerData, name)] = 'K';
        p[offsetof(ControllerData, index)] = (unsigned char)i;
        PutLE32(p + offsetof(ControllerData, field_0x210) +
                    (size_t)3 * sizeof(ControllerDataUnk0x210) +
                    offsetof(ControllerDataUnk0x210, present), 1);
        PutLE16(p + offsetof(ControllerData, field_0x2d8) + (size_t)4 * sizeof(unsigned short), 0x9abcu);
    }
    PutLE16(file + 0x11a0, 0);
    PutLE16(file + 0x11a2, 1);
}

static int CheckRecords(const ControllerData *records)
{
    int i;

    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        CHECK(records[i].field_0x0 == 0x11000000u + (unsigned)i);
        CHECK(records[i].field_0x4 == 0x22000000u + (unsigned)i);
        CHECK(records[i].field_0x10c == 0x33110000u + (unsigned)i);
        CHECK(records[i].field_0x128 == (unsigned short)(0x10u + (unsigned)i));
        CHECK(records[i].field_0x13c == (unsigned short)(0x20u + (unsigned)i));
        CHECK(records[i].name[0] == 'K');
        CHECK(records[i].index == (unsigned char)i);
        CHECK(records[i].field_0x210[3].present == 1);
        CHECK(records[i].field_0x2d8[4] == 0x9abc);
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char file[CONTROLLER_DISK_FILE_SIZE];
    ControllerData records[CONTROLLER_DISK_RECORDS];
    unsigned short mapping[CONTROLLER_DISK_MAPPINGS];
    int synthetic = 1;

    CHECK(sizeof(ControllerData) == 0x2f0);
    FillSynthetic(file);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(file, 1, sizeof(file), f);
        fclose(f);
        CHECK(got == CONTROLLER_DISK_FILE_SIZE);
        synthetic = 0;
    }

    memset(records, 0, sizeof(records));
    mapping[0] = mapping[1] = 0x5a5a;
    CHECK(ControllerDisk_DecodeAuto(records, mapping, file, sizeof(file)) != 0);
    CHECK(mapping[0] == 0);
    CHECK(mapping[1] == 1);
    if (synthetic) {
        if (CheckRecords(records))
            return 1;
    } else {
        // The captured file was written by the pre-fix big-endian build;
        // ControllerDisk_DecodeAuto must migrate it.
        CHECK(records[0].field_0x0 == 0x8000);
        CHECK(records[0].name[0] == 'K');
        CHECK(records[1].field_0x0 == 0x8000);
        CHECK(records[1].name[0] == 'K');
    }

    // Pack the native records back to the little-endian file form.
    {
        unsigned char packed[CONTROLLER_DISK_FILE_SIZE];
        ControllerData packedRecords[CONTROLLER_DISK_RECORDS];
        unsigned short packedMapping[CONTROLLER_DISK_MAPPINGS];

        CHECK(ControllerDisk_Pack(packed, sizeof(packed), records, mapping) != 0);
        memset(packedRecords, 0, sizeof(packedRecords));
        CHECK(ControllerDisk_Decode(packedRecords, packedMapping, packed, sizeof(packed)) != 0);
        CHECK(packedMapping[0] == 0 && packedMapping[1] == 1);
        if (synthetic) {
            if (CheckRecords(packedRecords))
                return 1;
        }
    }

    // An out-of-range mapping must reject the file and leave the outputs.
    PutLE16(file + 0x11a2, CONTROLLER_DISK_RECORDS);
    memset(records, 0, sizeof(records));
    mapping[0] = mapping[1] = 0x5a5a;
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file)) == 0);
    CHECK(mapping[0] == 0x5a5a && mapping[1] == 0x5a5a);
    CHECK(records[0].field_0x0 == 0);

    printf("controller_disk: OK\n");
    return 0;
}
