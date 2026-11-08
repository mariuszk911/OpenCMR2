// Stream textures for movie frames (Domain B: Amiga/MiniGL only).
//
// A stream texture holds RGBA8 pixels only in the GPU's own storage: it has no
// CPU copy, so Gfx_LockTexture fails on it and the renderer's dirty/upload
// path never touches it. The movie code keeps one persistent RGBA8 frame
// buffer, converts the changed macroblocks into it and uploads the changed row
// runs. It can be bound and drawn like any other texture (Gfx_SetTexture).
//
// The GL texture is padded to powers of two (640x480 -> 1024x512) and only its
// top-left corner holds the frame: MiniGL 29.1 on the PiStorm samples a
// non-power-of-two texture wrongly (tests/amiga/minigl_movie_upload_test.c
// records it). Draw the frame with texture coordinates (0, 0) to
// (width / glWidth, height / glHeight), from GfxMiniGL_GetStreamTextureSize.
//
// Two textures should be used alternately. MiniGL protects a texture only
// against being overwritten between draws of the same pass; a render that is
// still running from the previous frame can read a texture that is being
// overwritten in place, so a frame is uploaded to the texture that frame N-1
// did not draw from (see docs/VIDEO.md).
#ifndef OPENCMR2_GFX_MINIGL_MOVIE_H
#define OPENCMR2_GFX_MINIGL_MOVIE_H

#include "port/gfx.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A width x height RGBA8 texture (each side 1..4096), or NULL on failure. It
// has no pixels until the first GfxMiniGL_UploadRows. Free it with
// Gfx_DestroyTexture, which keeps the GL texture of the last two for the next
// stream textures of the same size (MiniGL keeps deleted texture memory until
// the context closes, so a new one per movie would add up).
GfxTexture *GfxMiniGL_CreateStreamTexture(int width, int height);

// The size of the GL texture behind a stream texture (powers of two). Returns 1
// on success, 0 when `texture` is not a stream texture.
int GfxMiniGL_GetStreamTextureSize(const GfxTexture *texture, int *glWidth, int *glHeight);

// Uploads rows [y0, y0 + rows) of `rgba`, the whole frame as tightly packed
// RGBA8 rows (pitch width * 4). The first upload to a texture always stores the
// whole frame, whatever the row range, because the GL texture is created from
// it (the rest of the padded texture is transparent black). Needs the display
// lock; takes it like any other renderer call. Returns 1
// on success, 0 on a bad argument or when the renderer is not available.
int GfxMiniGL_UploadRows(GfxTexture *texture, const uint8_t *rgba, int y0, int rows);

#ifdef __cplusplus
}
#endif

#endif
