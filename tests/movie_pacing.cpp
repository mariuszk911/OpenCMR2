// The pure movie timing and layout rules (src/video/movie_pacing.h).
#include "video/movie_pacing.h"

#include <cstdio>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void DueTimes()
{
    using MoviePacing::DueMs;
    Check(DueMs(0, 30, 1) == 0 && DueMs(1, 30, 1) == 33 && DueMs(3, 30, 1) == 100, "30 fps due times");
    Check(DueMs(1, 25, 1) == 40 && DueMs(549, 25, 1) == 21960, "25 fps due times");
    Check(DueMs(1, 30000, 1001) == 33 && DueMs(30000, 30000, 1001) == 1001000, "NTSC rate");
    // index * 1000 * denominator passes 32 bits; the 64-bit intermediate keeps it exact.
    Check(DueMs(5000000, 30, 1) == 166666666u, "long movies keep exact due times");
    Check(DueMs(5, 0, 1) == 0, "a zero rate never divides");
}

void ClockWraps()
{
    using MoviePacing::ElapsedMs;
    using MoviePacing::TooEarly;
    Check(ElapsedMs(0x10, 0xfffffff0u) == 0x20, "elapsed time across the wrap");
    Check(ElapsedMs(100, 150) == -50, "a start in the future is negative");
    Check(TooEarly(ElapsedMs(0x0000000f, 0xfffffff0u), 1, 30, 1), "frame 1 not yet due across the wrap");
    Check(!TooEarly(ElapsedMs(0x00000011, 0xfffffff0u), 1, 30, 1), "frame 1 due across the wrap");
    Check(TooEarly(-50, 0, 30, 1), "the latency wait holds the first frame");
    Check(!TooEarly(0, 0, 30, 1), "frame 0 is due at the start");
}

void Drops()
{
    using MoviePacing::ShouldDrop;
    // Frame 5 is due at 166 ms, frame 6 at 200 ms.
    Check(!ShouldDrop(true, true, 199, 5, 100, 0, 30, 1), "on time: shown");
    Check(ShouldDrop(true, true, 200, 5, 100, 0, 30, 1), "next frame due: dropped");
    Check(!ShouldDrop(false, true, 5000, 5, 100, 0, 30, 1), "dropping can be turned off");
    Check(!ShouldDrop(true, false, 5000, 5, 100, 0, 30, 1), "nothing drops before the clock starts");
    Check(!ShouldDrop(true, true, 5000, 0, 100, 0, 30, 1), "the first frame is always shown");
    Check(!ShouldDrop(true, true, 5000, 99, 100, 0, 30, 1), "the last frame is always shown");
    Check(ShouldDrop(true, true, 5000, 98, 100, 2, 30, 1), "two drops in a row allow a third");
    Check(!ShouldDrop(true, true, 5000, 98, 100, 3, 30, 1), "never more than three in a row");
    Check(!ShouldDrop(true, true, 5000, 0, 1, 0, 30, 1), "a one-frame movie shows its frame");
}

void Fits()
{
    using MoviePacing::Fit;
    using MoviePacing::Rect;
    Rect r = Fit({0, 0, 1920, 1080}, 640, 480, false);
    Check(r.x == 240 && r.y == 0 && r.width == 1440 && r.height == 1080, "wide area gets side bars");
    r = Fit({0, 0, 900, 1600}, 9, 9, false);
    Check(r.x == 0 && r.y == 350 && r.width == 900 && r.height == 900, "tall area gets top and bottom bars");
    r = Fit({0, 0, 1920, 1080}, 9, 9, false);
    Check(r.x == 420 && r.width == 1080, "the host test's square movie");
    r = Fit({0, 0, 640, 480}, 640, 480, false);
    Check(r.x == 0 && r.y == 0 && r.width == 640 && r.height == 480, "a matching area is unchanged");
    r = Fit({10, 20, 1920, 1080}, 640, 480, true);
    Check(r.x == 10 && r.y == 20 && r.width == 1920 && r.height == 1080, "stretch fills the area");
    r = Fit({0, 0, 0, 480}, 640, 480, false);
    Check(r.width == 0, "an empty area stays empty");
    r = Fit({0, 0, 641, 480}, 640, 480, false);
    Check(r.x == 0 && r.width == 640, "odd remainder rounds the bar down");
}

} // namespace

int main()
{
    DueTimes();
    ClockWraps();
    Drops();
    Fits();
    if (failures == 0)
        std::printf("movie_pacing: ok\n");
    return failures != 0;
}
