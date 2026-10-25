#include "port/disk.h"

#include <cstdio>
#include <cstdlib>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

extern "C" void Sys_Log(const char *format, ...)
{
    (void)format;
}

static void *Allocate(size_t size)
{
    void *pointer = calloc(1, size);
    return pointer;
}

int main()
{
    {
        unsigned char *buffer = (unsigned char *)Allocate(64);
        int *native = (int *)Allocate(sizeof(int));
        REQUIRE(buffer != NULL && native != NULL);
        Disk_RegisterBuffer(buffer, 64);
        Disk_AttachToBuffer(buffer + 8, native);
        Disk_ReleaseBuffer(buffer);
        free(buffer);
    }

    {
        /* Releasing one file buffer must not free the other's native copy. */
        unsigned char *a = (unsigned char *)Allocate(32);
        unsigned char *b = (unsigned char *)Allocate(32);
        int *nativeA = (int *)Allocate(sizeof(int));
        int *nativeB = (int *)Allocate(sizeof(int));
        REQUIRE(a != NULL && b != NULL && nativeA != NULL && nativeB != NULL);
        Disk_RegisterBuffer(a, 32);
        Disk_RegisterBuffer(b, 32);
        Disk_AttachToBuffer(a + 4, nativeA);
        Disk_AttachToBuffer(b + 4, nativeB);
        Disk_ReleaseBuffer(a);
        Disk_ReleaseBuffer(b);
        free(a);
        free(b);
    }

    {
        /* A later registration must replace the size used at attach time. */
        unsigned char *buffer = (unsigned char *)Allocate(32);
        int *native = (int *)Allocate(sizeof(int));
        REQUIRE(buffer != NULL && native != NULL);
        Disk_RegisterBuffer(buffer, 8);
        Disk_RegisterBuffer(buffer, 32);
        Disk_AttachToBuffer(buffer + 16, native);
        Disk_ReleaseBuffer(buffer);
        free(buffer);
    }

    {
        /* Repeated stage-style load/free must not leak or double free. */
        for (int i = 0; i < 1000; ++i) {
            unsigned char *buffer = (unsigned char *)Allocate(64);
            int *native = (int *)Allocate(sizeof(int));
            REQUIRE(buffer != NULL && native != NULL);
            Disk_RegisterBuffer(buffer, 64);
            Disk_AttachToBuffer(buffer + 1 + (size_t)(i % 7), native);
            Disk_ReleaseBuffer(buffer);
            free(buffer);
        }
    }

    {
        /* Buffers without attached native copies need no registration. */
        void *buffer = Allocate(8);
        REQUIRE(buffer != NULL);
        Disk_ReleaseBuffer(buffer);
        free(buffer);
    }

    return 0;
}
