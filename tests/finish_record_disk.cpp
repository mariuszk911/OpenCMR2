// fin.dat: little-endian barrier records decoded into native FinRecords.
#include "FinRecord.h"
#include "port/endian.h"
#include "port/finish_record_disk.h"

#include <stdio.h>
#include <string.h>

extern "C" void Sys_Log(const char *, ...) {}

static int Fail(const char *what, int line)
{
    printf("fin_record: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void FillRecord(unsigned char *p, unsigned int base)
{
    int i;

    for (i = 0; i < 10; i++)
        WriteLE32(p + (size_t)i * 4, base + (unsigned)i);
    WriteLE32(p + 0x28, 0x40 + base);
    p[0x2c] = (unsigned char)(0x19 + base);
    p[0x2d] = (unsigned char)(0x03 + base);
    p[0x2e] = (unsigned char)(0x11 + base);
    p[0x2f] = (unsigned char)(0x22 + base);
}

static int CheckRecord(const FinRecord *record, unsigned int base)
{
    int i;

    for (i = 0; i < 10; i++) {
        const unsigned char *field = (const unsigned char *)&record->target + (size_t)i * 4;
        unsigned int value;

        memcpy(&value, field, sizeof(value));
        CHECK(value == base + (unsigned)i);
    }
    CHECK(record->sector == (int)(0x40 + base));
    CHECK(record->type == (char)(0x19 + base));
    CHECK(record->flags == (unsigned char)(0x03 + base));
    CHECK(record->surfaces[0] == (unsigned char)(0x11 + base));
    CHECK(record->surfaces[1] == (unsigned char)(0x22 + base));
    return 0;
}

int main(void)
{
    unsigned char file[2 * FIN_RECORD_DISK_SIZE];
    unsigned char shifted[2 * FIN_RECORD_DISK_SIZE + 1];
    FinRecord native[2];
    unsigned int count = 0;

    FillRecord(file, 0x1000);
    FillRecord(file + FIN_RECORD_DISK_SIZE, 0x2000);
    memset(native, 0x5a, sizeof(native));
    CHECK(FinishRecordDisk_Decode(native, file, sizeof(file), &count) != 0);
    CHECK(count == 2);
    if (CheckRecord(&native[0], 0x1000))
        return 1;
    if (CheckRecord(&native[1], 0x2000))
        return 1;

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, file, sizeof(file));
    memset(native, 0, sizeof(native));
    count = 0;
    CHECK(FinishRecordDisk_Decode(native, shifted + 1, sizeof(file), &count) != 0);
    CHECK(count == 2);
    if (CheckRecord(&native[0], 0x1000))
        return 1;

    // A truncated record is rejected without touching the destination.
    memset(native, 0x5a, sizeof(native));
    count = 0x1234;
    CHECK(FinishRecordDisk_Decode(native, file, sizeof(file) - 1, &count) == 0);
    CHECK(count == 0x1234);
    CHECK(native[0].target.x == 0x5a5a5a5a);

    CHECK(FinishRecordDisk_Decode(NULL, file, sizeof(file), &count) == 0);
    CHECK(FinishRecordDisk_Decode(native, NULL, sizeof(file), &count) == 0);
    CHECK(FinishRecordDisk_Decode(native, file, 0, &count) == 0);

    printf("fin_record: OK\n");
    return 0;
}
