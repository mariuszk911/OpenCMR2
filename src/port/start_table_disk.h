/*
 * .csp stage start table.
 *
 * Layout: two 12-byte start points (x, y, z), the FixAngles (four u16) at
 * 0x18, then four more 12-byte start points at 0x20. The game's
 * Stage_GetCarStartXZ only addresses pStarts[0]/pStarts[3] (lanes 0 and 1);
 * the decoder converts the whole table so a same-size copy is safe.
 */
#ifndef OPENCMR2_PORT_START_TABLE_DISK_H
#define OPENCMR2_PORT_START_TABLE_DISK_H

#include <stddef.h>

#define START_TABLE_MIN_SIZE 0x20
#define START_TABLE_CONSUMED_ENTRIES 2

#ifdef __cplusplus
extern "C" {
#endif

/* destination must be at least size bytes. Returns 1 on success and sets
 * *outStartCount to the number of fully decoded 12-byte start points (2..6).
 * Returns 0 for a NULL input or a table shorter than 0x20 bytes. */
int StartTableDisk_Decode(void *destination, size_t destinationSize,
                          const void *file, size_t fileSize,
                          unsigned int *outStartCount);

#ifdef __cplusplus
}
#endif

#endif
