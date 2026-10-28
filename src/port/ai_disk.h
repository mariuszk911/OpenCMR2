/*
 * .ai0/.ai1/.ai2 CPU-driver tables.
 *
 * The selected AI member is copied into the stage buffer by
 * AI_LoadStageRouteTables; the parser then walks that private copy. It reads
 * the four section counts as the low byte of a 32-bit word (so those words
 * stay in file order) and every route/network/activation/node/reference value
 * as a native int. The decoder normalises those int and float regions in a
 * same-size copy, leaving the count words, descriptor bytes, point bytes and
 * node bytes untouched.
 */
#ifndef OPENCMR2_PORT_AI_DISK_H
#define OPENCMR2_PORT_AI_DISK_H

#include <stddef.h>

#define AI_DISK_CAR_COUNT 6
#define AI_DISK_ROUTE_STRIDE 0x88
#define AI_DISK_DESCRIPTOR_STRIDE 0x14
#define AI_DISK_POINT_STRIDE 0x10
#define AI_DISK_NETWORK_STRIDE 0x20
#define AI_DISK_NODE_COUNT 370
#define AI_DISK_NODE_STRIDE 12
#define AI_DISK_ACTIVATION_COUNT 64
#define AI_DISK_REFERENCE_ROUTE_SIZE 0x1720

#ifdef __cplusplus
extern "C" {
#endif

/* Normalises a same-size copy of the AI member. destinationSize must be at
 * least size bytes. Returns 1 on success; 0 for a NULL/short input or counts,
 * network sizes or section extents that do not fit the member. */
int AiDisk_Decode(void *destination, size_t destinationSize,
                  const void *file, size_t size);

#ifdef __cplusplus
}
#endif

#endif
