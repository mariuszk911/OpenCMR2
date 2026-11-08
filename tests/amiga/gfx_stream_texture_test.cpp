/*
 * Renderer stream-texture check outside the game (Domain B, target only).
 *
 * Opens the MiniGL renderer the way a small program does, straight with
 * Gfx_SetVideoMode and nothing before it: the case that crashed with a Line-A
 * Guru while AmigaSys_SetWindow used a NULL IntuitionBase
 * (docs/BINK_AMIGA_HANDOFF.md, section 5). Then it exercises the movie stream
 * textures (gfx_minigl_movie.h): creation, the padded GL size, refusal of
 * Gfx_LockTexture, a full upload, two row-run uploads, drawing and presenting
 * a few frames, and reuse of a destroyed texture's GL name. Run from a Shell:
 *
 *     amiga_gfx_stream_texture_test > RAM:gfx-stream.log
 *
 * Prints "stream: OK" or the first "stream: FAIL" line.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/gfx.h"
#include "port/sys.h"
#include "render/minigl/gfx_minigl_movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int kWidth = 640, kHeight = 480;

int Fail(const char *what)
{
    printf("stream: FAIL %s\n", what);
    Gfx_Shutdown();
    return 10;
}

void Fill(uint8_t *rgba, int y0, int rows, uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = y0; y < y0 + rows; ++y) {
        uint8_t *p = rgba + (size_t)y * kWidth * 4;
        for (int x = 0; x < kWidth; ++x, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Draw(GfxTexture *texture, int glWidth, int glHeight)
{
    GfxTLVertex v[4];
    memset(v, 0, sizeof(v));
    const float u1 = (kWidth - 0.5f) / glWidth, v1 = (kHeight - 0.5f) / glHeight;
    const float u0 = 0.5f / glWidth, v0 = 0.5f / glHeight;
    for (int i = 0; i < 4; ++i) {
        v[i].rhw = 1.0f;
        v[i].color = 0xffffffff;
    }
    v[0].sx = 0;      v[0].sy = 0;       v[0].tu = u0; v[0].tv = v0;
    v[1].sx = kWidth; v[1].sy = 0;       v[1].tu = u1; v[1].tv = v0;
    v[2].sx = 0;      v[2].sy = kHeight; v[2].tu = u0; v[2].tv = v1;
    v[3].sx = kWidth; v[3].sy = kHeight; v[3].tu = u1; v[3].tv = v1;
    Gfx_Clear(GFX_CLEAR_TARGET | GFX_CLEAR_ZBUFFER, 0xff000000, 1.0f);
    Gfx_BeginScene();
    Gfx_SetDepthTest(0);
    Gfx_SetAlphaBlend(0);
    Gfx_SetTexture(0, texture);
    Gfx_SetStageColorOp(0, GFX_TOP_SELECTARG1);
    Gfx_SetStageColorArg1(0, GFX_TA_TEXTURE);
    Gfx_DrawPrimitiveTL(GFX_TRIANGLESTRIP, v, 4);
    Gfx_EndScene();
    Gfx_Present(1);
}

} // namespace

int main(void)
{
    AmigaSys_SetCommandLine("amiga_gfx_stream_texture_test");
    int width = 0, height = 0;
    Gfx_GetDefaultDisplayMode(&width, &height);
    printf("stream: Gfx_SetVideoMode %dx%d fullscreen, nothing opened before\n", width, height);
    fflush(stdout);
    if (!Gfx_SetVideoMode(width, height, 1))
        return Fail("Gfx_SetVideoMode");
    printf("stream: video mode set\n");
    fflush(stdout);

    uint8_t *rgba = (uint8_t *)malloc((size_t)kWidth * kHeight * 4);
    if (rgba == NULL)
        return Fail("out of memory");
    GfxTexture *a = GfxMiniGL_CreateStreamTexture(kWidth, kHeight);
    int glWidth = 0, glHeight = 0;
    if (a == NULL || !GfxMiniGL_GetStreamTextureSize(a, &glWidth, &glHeight))
        return Fail("GfxMiniGL_CreateStreamTexture");
    if (glWidth != 1024 || glHeight != 512)
        return Fail("stream texture is not padded to 1024x512");
    GfxLockedRect rect;
    if (Gfx_LockTexture(a, 0, 0, &rect))
        return Fail("Gfx_LockTexture accepted a stream texture");
    if (GfxMiniGL_UploadRows(a, rgba, 0, kHeight + 1) || GfxMiniGL_UploadRows(NULL, rgba, 0, 1))
        return Fail("a bad argument was accepted");

    Fill(rgba, 0, kHeight, 200, 40, 40);
    if (!GfxMiniGL_UploadRows(a, rgba, 0, 16))
        return Fail("first upload");
    for (int i = 0; i < 25; ++i)
        Draw(a, glWidth, glHeight);
    Fill(rgba, 96, 48, 40, 200, 40);
    Fill(rgba, 400, 32, 40, 40, 200);
    const double start = Sys_GetTicksPrecise();
    if (!GfxMiniGL_UploadRows(a, rgba, 96, 48) || !GfxMiniGL_UploadRows(a, rgba, 400, 32))
        return Fail("row-run upload");
    const double uploadMs = Sys_GetTicksPrecise() - start;
    for (int i = 0; i < 25; ++i)
        Draw(a, glWidth, glHeight);

    // A destroyed stream texture leaves its GL name for the next one.
    Gfx_DestroyTexture(a);
    GfxTexture *b = GfxMiniGL_CreateStreamTexture(kWidth, kHeight);
    if (b == NULL || !GfxMiniGL_UploadRows(b, rgba, 0, 16))
        return Fail("reused stream texture");
    for (int i = 0; i < 25; ++i)
        Draw(b, glWidth, glHeight);
    Gfx_DestroyTexture(b);
    free(rgba);
    Gfx_Shutdown();
    printf("stream: OK, two row runs (80 rows) uploaded in %lu us\n", (unsigned long)(uploadMs * 1000.0));
    return 0;
}
