/*
 * Direct3D fixed-function vertex lighting on the CPU.
 *
 * minigl.library 29.x applies only one directional light and treats a
 * positional light as a direction (reVC found the same and lights on the CPU,
 * vendor/librw/src/mini/minilight.h). The MiniGL backend therefore lights on
 * the CPU every draw that a point or spot light reaches, with this function,
 * and draws the result unlit. The maths mirrors the host vertex shader
 * (src/render/shaders/fixed3d.vert), done in world space:
 *
 *   colour = clamp(E + A * (Ga + sum La * att * spot)
 *                    + D * sum Ld * max(0, N.L) * att * spot),  alpha = D.a
 *
 * with att = 1 / (a0 + a1 d + a2 d^2) (1 when the sum is 0), a light skipped
 * beyond its range, and the spot factor between cos(theta/2) and cos(phi/2)
 * raised to the falloff. Specular is not lit here.
 */
#ifndef OPENCMR2_PORT_D3D_LIGHT_H
#define OPENCMR2_PORT_D3D_LIGHT_H

#include "port/gfx.h"

#include <math.h>

/* Material colours the vertex is lit with, after the game's material sources
   have picked the material or a vertex colour for each. */
typedef struct D3DLightMaterial {
    GfxColorValue diffuse;
    GfxColorValue ambient;
    GfxColorValue emissive;
} D3DLightMaterial;

static inline float D3DLight_Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* pos: world-space position; n: world-space normal (normalised by the caller
   when the game asks for it). Lights with enabled[i] == 0 are skipped. */
static inline GfxColorValue D3DLight_Vertex(const GfxLight *lights, const int *enabled, int count,
                                            const GfxColorValue *globalAmbient,
                                            const D3DLightMaterial *m, const float pos[3], const float n[3])
{
    float ambR = globalAmbient->r, ambG = globalAmbient->g, ambB = globalAmbient->b;
    float difR = 0.0f, difG = 0.0f, difB = 0.0f;
    GfxColorValue out;
    int i;

    for (i = 0; i < count; ++i) {
        const GfxLight *l = &lights[i];
        float L[3];
        float scale = 1.0f;
        float nDotL;

        if (!enabled[i])
            continue;
        if (l->type == GFX_LIGHT_DIRECTIONAL) {
            const float len = sqrtf(l->direction.x * l->direction.x + l->direction.y * l->direction.y +
                                    l->direction.z * l->direction.z);
            if (len <= 0.0f)
                continue;
            L[0] = -l->direction.x / len;
            L[1] = -l->direction.y / len;
            L[2] = -l->direction.z / len;
        } else {
            const float tx = l->position.x - pos[0];
            const float ty = l->position.y - pos[1];
            const float tz = l->position.z - pos[2];
            const float d = sqrtf(tx * tx + ty * ty + tz * tz);
            float denom;

            if (l->range > 0.0f && d > l->range)
                continue;
            if (d > 0.0f) {
                L[0] = tx / d;
                L[1] = ty / d;
                L[2] = tz / d;
            } else {
                L[0] = 0.0f;
                L[1] = 0.0f;
                L[2] = 1.0f;
            }
            denom = l->attenuation0 + l->attenuation1 * d + l->attenuation2 * d * d;
            scale = denom > 0.0f ? 1.0f / denom : 1.0f;
            if (l->type == GFX_LIGHT_SPOT) {
                const float dl = sqrtf(l->direction.x * l->direction.x + l->direction.y * l->direction.y +
                                       l->direction.z * l->direction.z);
                const float inner = cosf(l->theta * 0.5f);
                const float outer = cosf(l->phi * 0.5f);
                const float rho = dl > 0.0f
                    ? -(L[0] * l->direction.x + L[1] * l->direction.y + L[2] * l->direction.z) / dl
                    : 1.0f;
                if (rho <= outer) {
                    scale = 0.0f;
                } else if (rho < inner) {
                    const float span = inner - outer > 1e-6f ? inner - outer : 1e-6f;
                    scale *= powf((rho - outer) / span, l->falloff);
                }
            }
        }
        ambR += l->ambient.r * scale;
        ambG += l->ambient.g * scale;
        ambB += l->ambient.b * scale;
        nDotL = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        if (nDotL > 0.0f) {
            difR += l->diffuse.r * nDotL * scale;
            difG += l->diffuse.g * nDotL * scale;
            difB += l->diffuse.b * nDotL * scale;
        }
    }
    out.r = D3DLight_Clamp01(m->emissive.r + ambR * m->ambient.r + difR * m->diffuse.r);
    out.g = D3DLight_Clamp01(m->emissive.g + ambG * m->ambient.g + difG * m->diffuse.g);
    out.b = D3DLight_Clamp01(m->emissive.b + ambB * m->ambient.b + difB * m->diffuse.b);
    out.a = m->diffuse.a;
    return out;
}

#endif
