// Pure timing and layout rules for movie playback (no platform calls), shared
// by the Amiga player and its host test. The host player (movie.cpp) follows
// the same rules inline; keep them identical.
#ifndef OPENCMR2_MOVIE_PACING_H
#define OPENCMR2_MOVIE_PACING_H

#include <stdint.h>

namespace MoviePacing {

// When frame `index` (0-based) is due, in milliseconds after the first frame
// was shown. The 64-bit intermediate keeps long movies exact.
inline uint32_t DueMs(uint32_t index, uint32_t fpsNumerator, uint32_t fpsDenominator)
{
    if (fpsNumerator == 0)
        return 0;
    return (uint32_t)((uint64_t)index * 1000u * fpsDenominator / fpsNumerator);
}

// Milliseconds from `start` to `now` on the wrapping 32-bit tick clock. Negative
// while `start` is still ahead (the clock is started in the future to wait for
// the audio output latency).
inline int32_t ElapsedMs(uint32_t now, uint32_t start)
{
    return (int32_t)(now - start);
}

// Movie_Wait: too early for frame `index`.
inline bool TooEarly(int32_t elapsed, uint32_t index, uint32_t fpsNumerator, uint32_t fpsDenominator)
{
    return (int64_t)elapsed < (int64_t)DueMs(index, fpsNumerator, fpsDenominator);
}

enum { kMaxConsecutiveDrops = 3 };

// Whether to skip showing frame `index` because the next one is already due.
// The first and the last frame are always shown, and never more than
// kMaxConsecutiveDrops in a row, so the picture keeps moving on a machine that
// is too slow for the movie.
inline bool ShouldDrop(bool enabled, bool clockStarted, int32_t elapsed, uint32_t index, uint32_t frames,
                       uint32_t consecutiveDrops, uint32_t fpsNumerator, uint32_t fpsDenominator)
{
    if (!enabled || !clockStarted || index == 0 || index + 1 >= frames ||
        consecutiveDrops >= kMaxConsecutiveDrops)
        return false;
    return !TooEarly(elapsed, index + 1, fpsNumerator, fpsDenominator);
}

struct Rect {
    int x, y, width, height;
};

// Fits a movieWidth x movieHeight picture into `area` keeping its proportions,
// centred with black bars; the same rounding as movie.cpp. Unchanged when
// stretching or when a size is zero.
inline Rect Fit(Rect area, uint32_t movieWidth, uint32_t movieHeight, bool stretch)
{
    if (stretch || movieWidth == 0 || movieHeight == 0 || area.width <= 0 || area.height <= 0)
        return area;
    const int64_t fitted = (int64_t)area.height * movieWidth / movieHeight;
    if (fitted <= area.width) {
        area.x += (area.width - (int)fitted) / 2;
        area.width = (int)fitted;
    } else {
        const int fittedHeight = (int)((int64_t)area.width * movieHeight / movieWidth);
        area.y += (area.height - fittedHeight) / 2;
        area.height = fittedHeight;
    }
    return area;
}

} // namespace MoviePacing

#endif
