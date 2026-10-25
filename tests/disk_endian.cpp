#include "port/disk.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

extern "C" void Sys_Log(const char *format, ...)
{
    (void)format;
}

int main()
{
    {
        static const unsigned char bytes[4] = {0x78, 0x56, 0x34, 0x12};
        REQUIRE(Disk_Offset(bytes, 0) == 0x12345678u);
    }

    {
        static const unsigned char run[4] = {0x01, 0x02, 0x03, 0x04};
        static const DiskRun runs[] = {{0, 0, 4}};
        static const DiskPointer pointers[] = {{4, 4}};
        static const DiskLayout layout = {
            8, 16, runs, 1, pointers, 1
        };
        unsigned char native[16] = {0};
        unsigned char record[8] = {0};
        void *pointer = reinterpret_cast<void *>(static_cast<uintptr_t>(0x12345678u));

        memcpy(native, run, sizeof(run));
        memcpy(native + 4, &pointer, sizeof(pointer));
        Disk_Pack(record, native, &layout);
        REQUIRE(memcmp(record, run, sizeof(run)) == 0);
        /* The fixture checks the stored order, not the host's. */
        REQUIRE(record[4] == 0x78 && record[5] == 0x56 && record[6] == 0x34 && record[7] == 0x12);

        memset(native, 0xaa, sizeof(native));
        Disk_Unpack(native, record, &layout);
        REQUIRE(memcmp(native, run, sizeof(run)) == 0);
        for (int i = 0; i < 8; ++i)
            REQUIRE(native[4 + i] == 0);
    }

    return 0;
}
