/*
 * .bsp stage-light placement: four little-endian FixVectors (right, up,
 * forward, position) consumed immediately by StageLights_SetTransform.
 */
#ifndef OPENCMR2_PORT_STAGE_LIGHT_TRANSFORM_DISK_H
#define OPENCMR2_PORT_STAGE_LIGHT_TRANSFORM_DISK_H

#include <stddef.h>

#define STAGE_LIGHT_TRANSFORM_DISK_SIZE 0x30

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes twelve ints into destination (at least 0x30 bytes). Returns 1 on
 * success, 0 for a NULL or short record. */
int StageLightTransformDisk_Decode(void *destination, size_t destinationSize,
                                   const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
