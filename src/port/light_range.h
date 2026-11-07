/*
 * Whether a Direct3D point or spot light reaches a draw.
 *
 * A Direct3D point or spot light lights nothing farther than its range. The
 * MiniGL backend lights a draw on the CPU (port/d3d_light.h) only when such a
 * light can reach it: the draw's world-space bounding sphere comes within the
 * range. These helpers compute that sphere and its distance to the light.
 */
#ifndef OPENCMR2_PORT_LIGHT_RANGE_H
#define OPENCMR2_PORT_LIGHT_RANGE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct LightRangeSphere {
    float x, y, z, radius;
} LightRangeSphere;

/* Bounding sphere (box centre, half diagonal) of the vertices a draw uses.
   `first` points at the x of vertex 0, `stride` is the vertex size in bytes;
   with `indices` only the indexed vertices count. Returns 0 when empty. */
static inline int LightRange_Bounds(const void *first, size_t stride, const uint16_t *indices,
                                    uint32_t count, LightRangeSphere *out)
{
    float lo[3];
    float hi[3];
    uint32_t i;

    if (count == 0)
        return 0;
    for (i = 0; i < count; ++i) {
        const uint32_t v = indices != NULL ? indices[i] : i;
        float p[3];
        int k;
        memcpy(p, (const unsigned char *)first + (size_t)v * stride, sizeof(p));
        for (k = 0; k < 3; ++k) {
            if (i == 0 || p[k] < lo[k])
                lo[k] = p[k];
            if (i == 0 || p[k] > hi[k])
                hi[k] = p[k];
        }
    }
    out->x = (lo[0] + hi[0]) * 0.5f;
    out->y = (lo[1] + hi[1]) * 0.5f;
    out->z = (lo[2] + hi[2]) * 0.5f;
    out->radius = 0.5f * sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                               (hi[2] - lo[2]) * (hi[2] - lo[2]));
    return 1;
}

/* Moves a model-space sphere to world space with a row-vector matrix
   (Direct3D layout, `m` = _11.._44). The radius grows by the largest axis
   scale, so a non-uniform scale still gives an enclosing sphere. */
static inline LightRangeSphere LightRange_ToWorld(LightRangeSphere s, const float *m)
{
    LightRangeSphere w;
    float scale = 0.0f;
    int r;

    w.x = s.x * m[0] + s.y * m[4] + s.z * m[8] + m[12];
    w.y = s.x * m[1] + s.y * m[5] + s.z * m[9] + m[13];
    w.z = s.x * m[2] + s.y * m[6] + s.z * m[10] + m[14];
    for (r = 0; r < 3; ++r) {
        const float axis = sqrtf(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]);
        if (axis > scale)
            scale = axis;
    }
    w.radius = s.radius * scale;
    return w;
}

/* Distance from the light to the nearest point of the sphere (0 inside it). */
static inline float LightRange_NearestDistance(float lightX, float lightY, float lightZ, LightRangeSphere s)
{
    const float dx = s.x - lightX;
    const float dy = s.y - lightY;
    const float dz = s.z - lightZ;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz) - s.radius;

    return d > 0.0f ? d : 0.0f;
}

#endif
