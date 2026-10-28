/*
 * .tm0/.tm1/.tm2 team split tables.
 *
 * Bytes 0x00..0x8f are the signed-byte split-spread table and are preserved.
 * From 0x90 to the end the table is 32-bit split samples/weights, so the
 * decoder swaps every whole word. The caller applies it to the private stage
 * buffer copy.
 */
#ifndef OPENCMR2_PORT_SPLIT_TABLE_DISK_H
#define OPENCMR2_PORT_SPLIT_TABLE_DISK_H

#include <stddef.h>

#define SPLIT_TABLE_BYTE_PREFIX 0x90

#ifdef __cplusplus
extern "C" {
#endif

/* destination must be at least size bytes. Returns 1 on success; 0 for a
 * NULL/short input or a table whose 32-bit region is not word-aligned. */
int SplitTableDisk_Decode(void *destination, size_t destinationSize,
                          const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
