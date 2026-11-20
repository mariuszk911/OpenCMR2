/*
 * The game's resolution list built from the display's screen modes (MiniGL on
 * the Amiga: the 32-bit RTG modes MiniGL can open a screen in).
 *
 * The list is sorted by width, then height, holds each size once and keeps at
 * most the game's 10 entries. Sizes under 640x480 are left out, as the game's
 * own check (CGraphics::EnumerateCompatibleDisplayModes) does. A full list
 * is thinned by DisplayModes_Select: 640x480, the largest mode and the
 * Workbench size stay, the rest are spread evenly. (DisplayModes_Trim is the
 * older rule that drops the second-smallest entry, as gfx_core's BuildModes
 * does.)
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

/* Reduces the sorted list to at most max entries. Always keeps the smallest,
 * the largest and the keepWidth x keepHeight size (the Workbench screen) when
 * the list holds it, then fills the remaining slots with evenly spaced
 * entries. Returns the new count; the list stays sorted. */
static inline int DisplayModes_Select(DisplayModeSize *list, int count, int max, int keepWidth, int keepHeight)
{
    unsigned char marked[DISPLAY_MODES_COLLECT_MAX];
    int marks = 0;
    int i;
    int kept = 0;

    if (count <= max || max < 1 || count > DISPLAY_MODES_COLLECT_MAX)
        return count;
    for (i = 0; i < count; ++i)
        marked[i] = 0;
    marked[0] = 1;
    marks = 1;
    if (max >= 2) {
        marked[count - 1] = 1;
        ++marks;
    }
    for (i = 0; i < count && marks < max; ++i) {
        if (!marked[i] && list[i].width == keepWidth && list[i].height == keepHeight) {
            marked[i] = 1;
            ++marks;
        }
    }
    {
        const int extra = max - marks;
        int k;
        for (k = 1; k <= extra; ++k) {
            const int target = k * (count - 1) / (extra + 1);
            int best = -1;
            int bestDist = count;
            for (i = 0; i < count; ++i) {
                const int d = i > target ? i - target : target - i;
                if (!marked[i] && d < bestDist) {
                    best = i;
                    bestDist = d;
                }
            }
            if (best >= 0)
                marked[best] = 1;
        }
    }
    for (i = 0; i < count; ++i) {
        if (marked[i])
            list[kept++] = list[i];
    }
    return kept;
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
