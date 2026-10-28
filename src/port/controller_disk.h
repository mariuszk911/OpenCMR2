/*
 * Controller.rcf: six native ControllerData records followed by two
 * little-endian slot mappings (file offset 0x11a0).
 *
 * The controllerData buffer must hold CONTROLLER_DISK_RECORDS records of the
 * native ControllerData layout. On failure the outputs are left untouched, so
 * the caller can keep its defaults.
 */
#ifndef OPENCMR2_PORT_CONTROLLER_DISK_H
#define OPENCMR2_PORT_CONTROLLER_DISK_H

#include <stddef.h>

#define CONTROLLER_DISK_FILE_SIZE 0x11a4
#define CONTROLLER_DISK_RECORDS 6
#define CONTROLLER_DISK_MAPPINGS 2

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 and fills both outputs for a valid 0x11a4-byte file whose slot
 * mappings index the six records; returns 0 otherwise. */
int ControllerDisk_Decode(void *controllerData, unsigned short *slotMapping,
                          const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
