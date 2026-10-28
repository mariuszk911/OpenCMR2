/*
 * fin.dat finish/barrier records (0x30 bytes each).
 *
 * The file record's +0x28 word is a sector index; the native FinRecord turns
 * it into an int there and later links the record through pNext. Disk_Unpack
 * zeroes the pointer and copies/swaps the scalar runs; the decoder restores
 * the sector index so the caller sees exactly the original loader state.
 */
#ifndef OPENCMR2_PORT_FINISH_RECORD_DISK_H
#define OPENCMR2_PORT_FINISH_RECORD_DISK_H

#include <stddef.h>

#define FIN_RECORD_DISK_SIZE 0x30

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes size/0x30 records into nativeRecords (an array of native FinRecord).
 * Returns 1 and sets *outCount on success; 0 for NULL/unaligned sizes. */
int FinishRecordDisk_Decode(void *nativeRecords, const void *file, size_t size,
                            unsigned int *outCount);

#ifdef __cplusplus
}
#endif

#endif
