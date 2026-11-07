/*
 * Stage lighting data (the .tre member): the light direction and basis, then
 * the light zones with their items, the shadow-mesh headers and the shadow
 * vertices, triangles and vertex light levels, all little-endian on disk.
 *
 * The member has no self-describing length and may be unaligned, so every
 * count is validated against its size and the records are decoded into
 * aligned native blocks that the caller owns and frees.
 *
 * Domain A (game layout): it uses the records of game/LightZone.h.
 */
#ifndef OPENCMR2_PORT_SCENE_LIGHTING_DISK_H
#define OPENCMR2_PORT_SCENE_LIGHTING_DISK_H

#include "port/types.h"
#include "LightZone.h"

struct SceneLightingDisk {
    int lightDir[3];                /* 16.16 light direction */
    int basis[3][3];                /* 16.16 light basis, row by row */
    unsigned int zoneCount;
    unsigned char *native;          /* LightZone[n], LightZoneMesh[n], LightZoneItem[...] */
    unsigned char *payload;         /* mesh vertices, triangles and light levels */
};

/* Nothing was decoded: NULL/short data or a zone table that does not fit. */
#define SCENE_LIGHTING_REJECTED 0
/* Direction and basis are valid; the zone tables failed. native/payload hold
 * whatever is still allocated (the caller stores both so the stage lighting
 * release frees them) and no zones may be used. */
#define SCENE_LIGHTING_HEADER_ONLY 1
/* The whole member decoded; zones are at native, in order. */
#define SCENE_LIGHTING_OK 2

int SceneLightingDisk_Decode(const unsigned char *file, unsigned int size,
                             SceneLightingDisk *out);

#endif
