/*
 * .cin car-info decoder.
 *
 * The file is a 12-byte directory of six little-endian u16 section offsets
 * followed by the six sections. The game caches pointers into the section
 * bytes (lights, contact patches, glass vertices, flexible lines, camera
 * offsets and the interior profile), so the decoder fills a same-size native
 * copy and the caller keeps that copy alive with the archive buffer.
 */
#ifndef OPENCMR2_PORT_CAR_INFO_DISK_H
#define OPENCMR2_PORT_CAR_INFO_DISK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes fileSize bytes from file into destination. destinationSize must be
 * at least fileSize; the result is a byte-identical copy with the consumed
 * scalars converted to native order and the section offsets decoded.
 * Returns 1 on success, 0 for a NULL/short input, non-monotonic offsets or a
 * section whose count does not fit its bounds. */
int CarInfoDisk_Decode(void *destination, size_t destinationSize,
                       const void *file, size_t fileSize);

#ifdef __cplusplus
}
#endif

#endif
