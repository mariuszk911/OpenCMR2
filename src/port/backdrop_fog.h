/*
 * Fog of a backdrop draw expressed as GL linear fog.
 *
 * Direct3D takes the fog factor from each vertex's specular alpha when fog is
 * on with neither vertex nor table fog; the sky and backdrop of a foggy stage
 * are drawn that way. Fixed-function GL has no per-vertex fog factor, but every
 * vertex of those meshes carries the same alpha, so one factor per draw is
 * enough. GL linear fog is f = (end - z) / (end - start); a range far larger
 * than any eye distance makes z negligible and leaves f = end / range.
 *
 * GL fog is used rather than mixing the fog colour into the vertex colour
 * because it applies after texturing, as Direct3D does: the backdrop's hills
 * are textured, so a vertex mix would multiply the fog by the texture.
 */
#ifndef OPENCMR2_PORT_BACKDROP_FOG_H
#define OPENCMR2_PORT_BACKDROP_FOG_H

/* 1e6 keeps a whole unit representable in a float at both ends of the range. */
#define BACKDROP_FOG_RANGE 1000000.0f

/* Linear fog limits giving a factor of alpha / 255 (0 all fog, 255 no fog). */
static inline void BackdropFog_Limits(unsigned int alpha, float *start, float *end)
{
    *end = (float)alpha * (BACKDROP_FOG_RANGE / 255.0f);
    *start = *end - BACKDROP_FOG_RANGE;
}

#endif
