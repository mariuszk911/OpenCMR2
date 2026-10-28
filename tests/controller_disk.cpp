// Controller.rcf: little-endian records and slot mappings decoded into native
// ControllerData records (port/controller_disk.h).
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
    printf("controller_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void FillRecord(unsigned char *file, int record)
{
    unsigned char *p = file + (size_t)record * sizeof(ControllerData);
    int i;

    memset(p, 0, sizeof(ControllerData));
    PutLE32(p + offsetof(ControllerData, field_0x0), 0x11000000u + (unsigned)record);
    PutLE32(p + offsetof(ControllerData, field_0x4), 0x22000000u + (unsigned)record);
    PutLE32(p + offsetof(ControllerData, field_0x10c), 0x33110000u + (unsigned)record);
    PutLE32(p + offsetof(ControllerData, field_0x124), 0x44220000u + (unsigned)record);
    PutLE16(p + offsetof(ControllerData, field_0x128), 0x1234u + (unsigned)record);
    PutLE16(p + offsetof(ControllerData, field_0x13c), 0x5678u + (unsigned)record);
    for (i = 0; i < 9; i++)
        p[offsetof(ControllerData, field_0x13e) + i] = (unsigned char)(0x40 + record * 9 + i);
    p[offsetof(ControllerData, name)] = 'K';
    p[offsetof(ControllerData, index)] = (unsigned char)record;
    PutLE32(p + offsetof(ControllerData, field_0x210) +
                (size_t)3 * sizeof(ControllerDataUnk0x210) +
                offsetof(ControllerDataUnk0x210, present), 1);
    PutLE16(p + offsetof(ControllerData, field_0x2d8) + (size_t)4 * sizeof(unsigned short), 0x9abcu);
}

static void FillFile(unsigned char *file, unsigned int mapping0, unsigned int mapping1)
{
    int i;

    memset(file, 0, CONTROLLER_DISK_FILE_SIZE);
    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++)
        FillRecord(file, i);
    PutLE16(file + 0x11a0, mapping0);
    PutLE16(file + 0x11a2, mapping1);
}

static int CheckRecords(const ControllerData *records)
{
    int i;

    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++) {
        CHECK(records[i].field_0x0 == 0x11000000u + (unsigned)i);
        CHECK(records[i].field_0x4 == 0x22000000u + (unsigned)i);
        CHECK(records[i].field_0x10c == 0x33110000u + (unsigned)i);
        CHECK(records[i].field_0x124 == 0x44220000u + (unsigned)i);
        CHECK(records[i].field_0x128 == (unsigned short)(0x1234u + (unsigned)i));
        CHECK(records[i].field_0x13c == (unsigned short)(0x5678u + (unsigned)i));
        CHECK(records[i].field_0x13e[0] == (unsigned char)(0x40 + i * 9));
        CHECK(records[i].field_0x13e[8] == (unsigned char)(0x40 + i * 9 + 8));
        CHECK(records[i].name[0] == 'K');
        CHECK(records[i].index == (unsigned char)i);
        CHECK(records[i].field_0x210[3].present == 1);
        CHECK(records[i].field_0x2d8[4] == 0x9abc);
    }
    return 0;
}

int main(void)
{
    unsigned char file[CONTROLLER_DISK_FILE_SIZE];
    unsigned char shifted[CONTROLLER_DISK_FILE_SIZE + 1];
    ControllerData records[CONTROLLER_DISK_RECORDS];
    unsigned short mapping[CONTROLLER_DISK_MAPPINGS];
    int i;

    CHECK(sizeof(ControllerData) == 0x2f0);

    FillFile(file, 0, 1);
    memset(records, 0x5a, sizeof(records));
    mapping[0] = mapping[1] = 0x5a5a;
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file)) != 0);
    CHECK(mapping[0] == 0);
    CHECK(mapping[1] == 1);
    if (CheckRecords(records))
        return 1;

    // An unaligned source must decode identically.
    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(records, 0, sizeof(records));
    mapping[0] = mapping[1] = 0x5a5a;
    CHECK(ControllerDisk_Decode(records, mapping, shifted + 1, sizeof(file)) != 0);
    CHECK(mapping[0] == 0);
    CHECK(mapping[1] == 1);
    if (CheckRecords(records))
        return 1;

    // The mapping indexes the six records; an out-of-range entry rejects the
    // file without touching the outputs.
    FillFile(file, 0, CONTROLLER_DISK_RECORDS);
    memset(records, 0, sizeof(records));
    mapping[0] = mapping[1] = 0x5a5a;
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file)) == 0);
    CHECK(mapping[0] == 0x5a5a && mapping[1] == 0x5a5a);
    for (i = 0; i < CONTROLLER_DISK_RECORDS; i++)
        CHECK(records[i].field_0x0 == 0);

    FillFile(file, CONTROLLER_DISK_RECORDS, 0);
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file)) == 0);

    // Wrong sizes and NULL inputs are rejected.
    FillFile(file, 0, 1);
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file) - 1) == 0);
    CHECK(ControllerDisk_Decode(records, mapping, file, sizeof(file) + 1) == 0);
    CHECK(ControllerDisk_Decode(records, mapping, NULL, sizeof(file)) == 0);
    CHECK(ControllerDisk_Decode(NULL, mapping, file, sizeof(file)) == 0);
    CHECK(ControllerDisk_Decode(records, NULL, file, sizeof(file)) == 0);

    printf("controller_disk: OK\n");
    return 0;
}
