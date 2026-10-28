/*
 * .cod co-driver call table: an array of little-endian 32-bit call records.
 * RallyRouteNode.callId indexes this table (signed -1 is the no-call
 * sentinel), so the decoder fills a native copy attached to the archive.
 */
#ifndef OPENCMR2_PORT_COD_DISK_H
#define OPENCMR2_PORT_COD_DISK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes size/4 ints into nativeWords (at least size bytes). Returns 1 and
 * sets *outCount on success; 0 for NULL input or a size that is not a whole
 * number of 32-bit words. */
int CodDisk_Decode(void *nativeWords, size_t nativeSize,
                   const void *file, size_t size,
                   unsigned int *outCount);

#ifdef __cplusplus
}
#endif

#endif
