// [video] renderer parsing and the fall-back decision (port/gfx_renderer.h).
#include "port/gfx_renderer.h"

#include <cstdio>
#include <cstring>

namespace {
int failures;
void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
}

int main()
{
    int known = 0;
    Check(GfxRenderer_Parse(0, &known) == GFX_RENDERER_MINIGL && known, "NULL is the default");
    Check(GfxRenderer_Parse("", &known) == GFX_RENDERER_MINIGL && known, "empty is the default");
    Check(GfxRenderer_Parse("minigl", &known) == GFX_RENDERER_MINIGL && known, "minigl");
    Check(GfxRenderer_Parse("MiniGL", &known) == GFX_RENDERER_MINIGL && known, "MiniGL case");
    Check(GfxRenderer_Parse("gles2", &known) == GFX_RENDERER_GLES2 && known, "gles2");
    Check(GfxRenderer_Parse("  GLES2 \r\n", &known) == GFX_RENDERER_GLES2 && known, "blanks and case");
    Check(GfxRenderer_Parse("gles", &known) == GFX_RENDERER_MINIGL && !known, "prefix is unknown");
    Check(GfxRenderer_Parse("gles22", &known) == GFX_RENDERER_MINIGL && !known, "longer is unknown");
    Check(GfxRenderer_Parse("opengl", &known) == GFX_RENDERER_MINIGL && !known, "other is unknown");
    Check(GfxRenderer_Parse("gles2", 0) == GFX_RENDERER_GLES2, "known may be NULL");

    const char *why = "stale";
    Check(GfxRenderer_Choose(GFX_RENDERER_MINIGL, 1, 1, 1, &why) == GFX_RENDERER_MINIGL && why == 0,
          "minigl stays minigl without a reason");
    Check(GfxRenderer_Choose(GFX_RENDERER_GLES2, 1, 1, 1, &why) == GFX_RENDERER_GLES2 && why == 0,
          "gles2 when everything is there");
    Check(GfxRenderer_Choose(GFX_RENDERER_GLES2, 0, 1, 1, &why) == GFX_RENDERER_MINIGL && why != 0 &&
              std::strstr(why, "not built") != 0, "not built falls back");
    Check(GfxRenderer_Choose(GFX_RENDERER_GLES2, 1, 0, 1, &why) == GFX_RENDERER_MINIGL && why != 0 &&
              std::strstr(why, "ogles2.library") != 0, "no library falls back");
    Check(GfxRenderer_Choose(GFX_RENDERER_GLES2, 1, 1, 0, &why) == GFX_RENDERER_MINIGL && why != 0 &&
              std::strstr(why, "32-bit") != 0, "no mode falls back");
    Check(GfxRenderer_Choose(GFX_RENDERER_GLES2, 1, 1, 1, 0) == GFX_RENDERER_GLES2, "reason may be NULL");
    if (failures == 0)
        std::puts("gfx_renderer: ok");
    return failures != 0;
}
