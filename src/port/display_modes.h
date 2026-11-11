/*
 * The game's resolution list built from the display's screen modes (MiniGL on
 * the Amiga: the 32-bit RTG modes MiniGL can open a screen in).
 *
 * The list is sorted by width, then height, holds each size once and keeps at
 * most the game's 10 entries. Sizes under 640x480 are left out, as the game's
 * own check (CGraphics::EnumerateCompatibleDisplayModes) does. A full list
 * drops its second-smallest entry, as gfx_core's BuildModes does, so 640x480
 * and the largest modes stay.
 */
#ifndef OPENCMR2_PORT_DISPLAY_MODES_H
#define OPENCMR2_PORT_DISPLAY_MODES_H

enum { DISPLAY_MODES_GAME_MAX = 10, DISPLAY_MODES_COLLECT_MAX = 64 };

typedef struct DisplayModeSize {
    int width;
    int height;
} DisplayModeSize;

static inline int DisplayModes_Less(int w0, int h0, int w1, int h1)
{
    return w0 < w1 || (w0 == w1 && h0 < h1);
}

/* Inserts width x height into the sorted list of count entries (room for
 * capacity). Returns the new count; a duplicate, a size under 640x480, or a
 * full list leaves it unchanged. */
static inline int DisplayModes_Add(DisplayModeSize *list, int count, int capacity, int width, int height)
{
    int at;
    int i;

    if (width < 640 || height < 480 || count >= capacity)
        return count;
    for (at = 0; at < count; ++at) {
        if (list[at].width == width && list[at].height == height)
            return count;
        if (DisplayModes_Less(width, height, list[at].width, list[at].height))
            break;
    }
    for (i = count; i > at; --i)
        list[i] = list[i - 1];
    list[at].width = width;
    list[at].height = height;
    return count + 1;
}

/* Drops entries until at most max remain (max >= 1), removing the
 * second-smallest each time. Returns the new count. */
static inline int DisplayModes_Trim(DisplayModeSize *list, int count, int max)
{
    int i;

    while (count > max && count > 1) {
        for (i = 1; i + 1 < count; ++i)
            list[i] = list[i + 1];
        --count;
    }
    return count;
}

/* Keeps only the sizes that fit in maxWidth x maxHeight. Returns the count. */
static inline int DisplayModes_Fit(DisplayModeSize *list, int count, int maxWidth, int maxHeight)
{
    int kept = 0;
    int i;

    for (i = 0; i < count; ++i) {
        if (list[i].width <= maxWidth && list[i].height <= maxHeight)
            list[kept++] = list[i];
    }
    return kept;
}

#endif
