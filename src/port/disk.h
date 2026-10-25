/*
 * Records the game loads from files whose 32-bit offsets the original turns
 * into pointers in place. On 64-bit targets the structures holding those
 * pointers are larger than the file records, so the port builds native
 * copies: Disk_Unpack copies a record's pointer-free bytes to the native
 * layout (port/disk_layouts.h, generated) and leaves its pointer fields NULL
 * for the loader to relocate. The same code runs on 32-bit targets, where the
 * layouts are the same.
 */
#ifndef OPENCMR2_PORT_DISK_H
#define OPENCMR2_PORT_DISK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "port/align.h"

struct OPENCMR2_PORT_ALIGN4 DiskRun {
    unsigned short disk, native, size;
};

struct OPENCMR2_PORT_ALIGN4 DiskPointer {
    unsigned short disk, native;
};

struct OPENCMR2_PORT_ALIGN4 DiskLayout {
    unsigned short diskSize;
    unsigned short nativeSize;
    const DiskRun *runs;
    int runCount;
    const DiskPointer *pointers;
    int pointerCount;
};

/* The 32-bit value at offset off of a file record. -1 means none. */
static inline uint32_t Disk_Offset(const void *record, unsigned off)
{
    uint32_t value;
    memcpy(&value, (const char *)record + off, sizeof(value));
    return value;
}

/* Reports an offset that is not a record of the array it should point into. */
void Disk_BadOffset(const void *record, unsigned off, uint32_t value);

#ifdef __cplusplus
/* The native record a relocated file offset points to: the file array holds
 * records of diskSize bytes, the native one count elements. -1 is none. */
template <typename T>
static inline T *Disk_Record(const void *record, unsigned off, T *array, unsigned diskSize, unsigned count)
{
    uint32_t value = Disk_Offset(record, off);
    if (value == 0xffffffffu)
        return NULL;
    if (value % diskSize != 0 || value / diskSize >= count) {
        Disk_BadOffset(record, off, value);
        return NULL;
    }
    return &array[value / diskSize];
}
#define DISK_RECORD(record, off, array, Type, count) \
    Disk_Record<Type>((record), (off), (array), g_diskLayout_##Type.diskSize, (count))
#endif

/* A relocated offset into pointer-free file data at base. -1 is none. */
static inline void *Disk_Data(const void *record, unsigned off, void *base)
{
    uint32_t value = Disk_Offset(record, off);
    return value == 0xffffffffu ? NULL : (char *)base + value;
}
#define DISK_DATA(record, off, base) Disk_Data((record), (off), (base))

/* Copies a file record to native storage; pointer fields become NULL. */
void Disk_Unpack(void *native, const void *record, const DiskLayout *layout);

/* The reverse: writes a native record in the file layout; pointer fields
 * keep the low 32 bits of the pointer (addresses are not comparable anyway). */
void Disk_Pack(void *record, const void *native, const DiskLayout *layout);

/* Frees native storage together with the game buffer that holds fileData:
 * CFileBuffer::FreeGenericFileBuffer of that buffer frees it. */
void Disk_AttachToBuffer(const void *fileData, void *native);
/* Records the exact size of a file buffer that can own attached native
 * copies. CFileBuffer::GetGenericFileBuffer calls this before returning the
 * buffer; the size replaces malloc_usable_size on targets without it. A
 * second call for the same buffer updates the size. */
void Disk_RegisterBuffer(void *buffer, size_t size);
/* Called with every game buffer about to be freed. Releases the native copies
 * attached to that buffer and forgets its registration; the caller then frees
 * the buffer itself. Attachments are owned by the registered buffer that
 * contained their file data, so a buffer that never owned any releases
 * nothing; Disk_AttachToBuffer reports data outside every registered buffer. */
void Disk_ReleaseBuffer(void *buffer);

#endif
