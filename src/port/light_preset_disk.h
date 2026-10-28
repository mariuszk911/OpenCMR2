/*
 * .hor time-of-day light presets.
 *
 * Each 0x4e-byte record starts with seven ints (mesh-height parameters, the
 * field at 0x0c, sun icon size and the fog start/end). The colour-correction
 * tail (0x1c..0x4d) is bytes and stays unchanged.
 */
#ifndef OPENCMR2_PORT_LIGHT_PRESET_DISK_H
#define OPENCMR2_PORT_LIGHT_PRESET_DISK_H

#include <stddef.h>

#define LIGHT_PRESET_DISK_SIZE 0x4e

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes one record into destination (at least 0x4e bytes). Returns 1 on
 * success, 0 for a NULL or short record. */
int LightPresetDisk_Decode(void *destination, size_t destinationSize,
                           const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
