#ifndef OPENCMR2_PORT_WEATHER_MAP_H
#define OPENCMR2_PORT_WEATHER_MAP_H

/*
 * Number of Texture* entries in WeatherMapSet::maps (game/RallyData.cpp).
 * Keep in sync with that structure: the original 0x30-byte block puts rects[]
 * and slots[] directly after maps[], so the array cannot grow.
 */
enum { WEATHER_MAP_SLOTS = 4 };

/*
 * Weather map index for preview panel `panel` (0 or 1) of stage `stage`.
 *
 * The original computes `(stage % 4) + panel`, which reaches 4 for stage
 * residue 3 and a second panel and reads maps[4] past the array. `mapCount`
 * is the number of maps loaded for the current rally group (2..4). Clamp to
 * the last loaded map: the panel's stage index is used as a range end, so
 * wrapping to 0 would invert that range.
 */
static inline int WeatherMap_Index(int stage, int panel, int mapCount)
{
    int index;

    if (mapCount < 1)
        mapCount = 1;
    else if (mapCount > WEATHER_MAP_SLOTS)
        mapCount = WEATHER_MAP_SLOTS;
    index = (stage & 0xff) % WEATHER_MAP_SLOTS + panel;
    if (index >= mapCount)
        index = mapCount - 1;
    return index;
}

#endif
