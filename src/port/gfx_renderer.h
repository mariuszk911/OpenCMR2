/*
 * [video] renderer = minigl | gles2 (AmigaOS): the parsing of the ini value
 * and the fall-back decision, host-testable. The Amiga dispatcher
 * (render/gfx_dispatch_amiga.cpp) feeds it what it found on the machine.
 */
#ifndef OPENCMR2_PORT_GFX_RENDERER_H
#define OPENCMR2_PORT_GFX_RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

enum GfxRendererKind {
    GFX_RENDERER_MINIGL = 0,
    GFX_RENDERER_GLES2 = 1
};

static inline int GfxRenderer_Lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* Parses the ini text (case-insensitive, surrounding blanks ignored). NULL
   and "" mean the default, minigl. *known is 0 for any other text, which
   also gives minigl. */
static inline int GfxRenderer_Parse(const char *text, int *known)
{
    static const char kMiniGL[] = "minigl";
    static const char kGles2[] = "gles2";
    const char *start = text != 0 ? text : "";
    const char *end;
    int result = GFX_RENDERER_MINIGL;
    int isKnown = 0;
    unsigned length;

    while (*start == ' ' || *start == '\t')
        ++start;
    end = start;
    while (*end != '\0')
        ++end;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        --end;
    length = (unsigned)(end - start);
    if (length == 0) {
        isKnown = 1;
    } else {
        unsigned i;
        int matchMiniGL = length == sizeof(kMiniGL) - 1;
        int matchGles2 = length == sizeof(kGles2) - 1;
        for (i = 0; i < length; ++i) {
            const int c = GfxRenderer_Lower((unsigned char)start[i]);
            if (matchMiniGL && c != kMiniGL[i])
                matchMiniGL = 0;
            if (matchGles2 && c != kGles2[i])
                matchGles2 = 0;
        }
        if (matchMiniGL) {
            isKnown = 1;
        } else if (matchGles2) {
            isKnown = 1;
            result = GFX_RENDERER_GLES2;
        }
    }
    if (known != 0)
        *known = isKnown;
    return result;
}

/* What the dispatcher ends up with. `reason` is a static sentence for the log
   when GLES2 was asked for and not used (NULL otherwise). */
static inline int GfxRenderer_Choose(int requested, int gles2Built, int ogles2Opens, int modeFound,
                                     const char **reason)
{
    const char *why = 0;
    int chosen = GFX_RENDERER_MINIGL;

    if (requested == GFX_RENDERER_GLES2) {
        if (!gles2Built)
            why = "the GLES2 renderer is not built into this binary";
        else if (!ogles2Opens)
            why = "ogles2.library 1.3.3 or newer could not be opened";
        else if (!modeFound)
            why = "no 32-bit RTG screen mode was found";
        else
            chosen = GFX_RENDERER_GLES2;
    }
    if (reason != 0)
        *reason = why;
    return chosen;
}

#ifdef __cplusplus
}
#endif

#endif
