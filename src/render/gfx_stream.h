/*
 * Stream textures for movie frames, the same on every Amiga renderer (Domain B).
 *
 * A stream texture holds RGBA8 pixels in the GPU's own storage only: it has no
 * CPU copy, so Gfx_LockTexture fails on it. The movie code keeps one persistent
 * RGBA8 frame buffer, converts the changed macroblocks into it and uploads the
 * changed row runs. The texture can be bound and drawn like any other
 * (Gfx_SetTexture). The GL texture may be larger than the frame (MiniGL pads
 * to powers of two, see render/minigl/gfx_minigl_movie.h): the frame is its
 * top-left width x height, drawn with texture coordinates (0, 0) to
 * (width / glWidth, height / glHeight) from GfxStream_GetSize. Two textures
 * should be used alternately (docs/VIDEO.md).
 *
 * Implemented by render/gfx_dispatch_amiga.cpp for the renderer in use.
 */
#ifndef OPENCMR2_RENDER_GFX_STREAM_H
#define OPENCMR2_RENDER_GFX_STREAM_H

#include "port/gfx.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A width x height RGBA8 stream texture (each side 1..4096), or NULL. It has no
   pixels until the first GfxStream_UploadRows. Free it with Gfx_DestroyTexture. */
GfxTexture *GfxStream_Create(int width, int height);
/* The size of the GL texture behind it. 1 on success, 0 when texture is not a
   stream texture. */
int GfxStream_GetSize(const GfxTexture *texture, int *glWidth, int *glHeight);
/* Uploads rows [y0, y0 + rows) of rgba, the whole frame as tightly packed RGBA8
   rows (pitch width * 4). The first upload always stores the whole frame.
   Returns 1 on success, 0 on a bad argument or when the renderer is not up. */
int GfxStream_UploadRows(GfxTexture *texture, const uint8_t *rgba, int y0, int rows);

#ifdef __cplusplus
}
#endif

#endif
