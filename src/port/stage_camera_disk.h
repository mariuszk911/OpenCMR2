/*
 * .tsc trackside cameras.
 *
 * The file starts with a little-endian int count and then count CameraSpot
 * records of 0x6c bytes. StageObject_SelectNonemptyRecordList keeps a pointer
 * into this list, so the decoder fills a same-size native copy owned by the
 * archive member's buffer.
 */
#ifndef OPENCMR2_PORT_STAGE_CAMERA_DISK_H
#define OPENCMR2_PORT_STAGE_CAMERA_DISK_H

#include <stddef.h>
#include <stdint.h>

#define STAGE_CAMERA_RECORD_SIZE 0x6c

#ifdef __cplusplus
extern "C" {
#endif

/* destination must be at least fileSize bytes. On success *outCount (when
 * non-NULL) receives the decoded record count. Returns 0 for a NULL/short
 * input or a count that does not fit the file. */
int StageCameraDisk_Decode(void *destination, size_t destinationSize,
                           const void *file, size_t fileSize,
                           uint32_t *outCount);

#ifdef __cplusplus
}
#endif

#endif
