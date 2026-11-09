/*
 * Display mode choice for the ogles2 renderer (Domain B, AmigaOS only).
 *
 * ogles2's full-screen context (OGLES2_CCT_MODEID) refuses anything but a
 * 32-bit RTG mode (OGLES2_CCE_NO_DISPLAY, error 4); graphics.library's
 * BestModeID offers 24-bit modes first. The Workbench screen's own mode is
 * the one proven on the target, so that is tried first.
 */
#ifndef OPENCMR2_RENDER_GLES2_OGLES2_DISPLAY_H
#define OPENCMR2_RENDER_GLES2_OGLES2_DISPLAY_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ogles2Mode {
    ULONG modeId;
    ULONG width;
    ULONG height;
    ULONG depth;
} Ogles2Mode;

/* Finds a 32-bit RTG mode: the default public screen's when it is one, else
   the best 32-bit mode of at least wantWidth x wantHeight (0, 0: any). It
   opens and closes the libraries it needs itself. On failure it returns FALSE
   and sets *why to a static sentence for the log. */
BOOL Ogles2FindMode(ULONG wantWidth, ULONG wantHeight, Ogles2Mode *out, const char **why);

#ifdef __cplusplus
}
#endif

#endif
