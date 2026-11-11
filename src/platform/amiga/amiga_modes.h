/* The RTG screen sizes MiniGL can open a fullscreen display in. */
#ifndef OPENCMR2_AMIGA_MODES_H
#define OPENCMR2_AMIGA_MODES_H

#include "port/display_modes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fills list (sorted, unique, >= 640x480, room for capacity) with the sizes
 * of the display database's PIXFMT_BGRA32 modes. graphics.library and
 * DimensionInfo report these modes as 24-bit (colour bits only), so the pixel
 * format is the test, as in MiniGL's vid_OpenDisplay. Returns the count; 0
 * without cybergraphics.library or such modes. */
int AmigaModes_ListBGRA32(DisplayModeSize *list, int capacity);

#ifdef __cplusplus
}
#endif

#endif
