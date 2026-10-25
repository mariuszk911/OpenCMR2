// Amiga v1 keeps the original visuals: the generated outline-font and vector
// sprite replacements are host-only enhancements and stay disabled.
#include "port/outline_font.h"
#include "port/vector_sprite.h"

extern "C" void OutlineFont_Replace(const char *name, GfxTexture *texture,
                                    const OutlineFontGlyph *glyphs, int count)
{
    (void)name;
    (void)texture;
    (void)glyphs;
    (void)count;
}

extern "C" void VectorSprite_Replace(const char *name, GfxTexture *texture)
{
    (void)name;
    (void)texture;
}
