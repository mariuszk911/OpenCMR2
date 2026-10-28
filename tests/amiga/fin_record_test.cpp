// Real 68060 coverage for the fin.dat record decoder.
#include "FinRecord.h"
#include "port/disk_layouts.h"
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

static FinRecord s_records[1000];

int main(int argc, char **argv)
{
    unsigned char file[2 * FIN_RECORD_DISK_SIZE];
    FinRecord native[2];
    unsigned char captured[0x10000];
    unsigned int count = 0;

    CHECK(sizeof(FinRecord) == 0x30);
    CHECK(g_diskLayout_FinRecord.diskSize == 0x30);

    FillRecord(file, 0x1000);
    FillRecord(file + FIN_RECORD_DISK_SIZE, 0x2000);
    memset(native, 0, sizeof(native));
    CHECK(FinishRecordDisk_Decode(native, file, sizeof(file), &count) != 0);
    CHECK(count == 2);
    if (CheckRecord(&native[0], 0x1000))
        return 1;
    if (CheckRecord(&native[1], 0x2000))
        return 1;

    memset(native, 0x5a, sizeof(native));
    CHECK(FinishRecordDisk_Decode(native, file, sizeof(file) - 1, &count) == 0);
    CHECK(native[0].target.x == 0x5a5a5a5a);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        size_t got;

        CHECK(f != NULL);
        got = fread(captured, 1, sizeof(captured), f);
        fclose(f);
        CHECK(got == 893u * FIN_RECORD_DISK_SIZE);
        memset(s_records, 0, sizeof(s_records));
        CHECK(FinishRecordDisk_Decode(s_records, captured, got, &count) != 0);
        CHECK(count == 893);
        CHECK(s_records[0].target.x == 15374937);
        CHECK(s_records[0].direction.z == 45825);
        CHECK(s_records[0].field_0x24 == -2120837316);
        CHECK(s_records[0].sector == 4);
        CHECK(s_records[0].type == 0x13);
        CHECK(s_records[0].flags == 1);
        CHECK(s_records[0].surfaces[0] == 0xff);
        CHECK(s_records[0].surfaces[1] == 0x4a);
    }

    printf("fin_record: OK\n");
    return 0;
}
