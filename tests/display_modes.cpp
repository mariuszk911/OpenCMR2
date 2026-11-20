// The resolution list the MiniGL renderer builds from the Amiga's screen modes
// (port/display_modes.h): sorted, unique, at least 640x480, at most 10.
#include "port/display_modes.h"

#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    DisplayModeSize list[DISPLAY_MODES_COLLECT_MAX];
    int n = 0;

    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 800, 600);
    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 640, 480);
    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 800, 600);  // duplicate
    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 320, 240);  // too small
    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 640, 512);
    n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, 1280, 720);
    REQUIRE(n == 4);
    REQUIRE(list[0].width == 640 && list[0].height == 480);
    REQUIRE(list[1].width == 640 && list[1].height == 512);
    REQUIRE(list[2].width == 800 && list[2].height == 600);
    REQUIRE(list[3].width == 1280 && list[3].height == 720);

    // A full list is unchanged.
    REQUIRE(DisplayModes_Add(list, n, n, 1024, 768) == n);

    // Fit to an 800x600 Workbench.
    DisplayModeSize fit[DISPLAY_MODES_COLLECT_MAX];
    for (int i = 0; i < n; ++i)
        fit[i] = list[i];
    REQUIRE(DisplayModes_Fit(fit, n, 800, 600) == 3);
    REQUIRE(fit[2].width == 800);

    // Trim keeps the smallest and the largest.
    n = 0;
    for (int w = 640; w <= 640 + 15 * 64; w += 64)
        n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, w, 480);
    REQUIRE(n == 16);
    n = DisplayModes_Trim(list, n, DISPLAY_MODES_GAME_MAX);
    REQUIRE(n == DISPLAY_MODES_GAME_MAX);
    REQUIRE(list[0].width == 640);
    REQUIRE(list[n - 1].width == 640 + 15 * 64);
    for (int i = 1; i < n; ++i)
        REQUIRE(DisplayModes_Less(list[i - 1].width, list[i - 1].height, list[i].width, list[i].height));

    // Select keeps the smallest, the largest and the Workbench size, and
    // spreads the rest.
    n = 0;
    for (int w = 640; w < 640 + 30 * 32; w += 32)
        n = DisplayModes_Add(list, n, DISPLAY_MODES_COLLECT_MAX, w, 480);
    REQUIRE(n == 30);
    n = DisplayModes_Select(list, n, DISPLAY_MODES_GAME_MAX, 640 + 3 * 32, 480);
    REQUIRE(n == DISPLAY_MODES_GAME_MAX);
    REQUIRE(list[0].width == 640);
    REQUIRE(list[n - 1].width == 640 + 29 * 32);
    bool hasWb = false;
    for (int i = 0; i < n; ++i)
        hasWb = hasWb || list[i].width == 640 + 3 * 32;
    REQUIRE(hasWb);
    for (int i = 1; i < n; ++i)
        REQUIRE(DisplayModes_Less(list[i - 1].width, list[i - 1].height, list[i].width, list[i].height));
    // A short list, or a Workbench size not in it, is handled.
    REQUIRE(DisplayModes_Select(list, 4, DISPLAY_MODES_GAME_MAX, 1, 1) == 4);
    n = 30;
    for (int i = 0; i < n; ++i) { list[i].width = 640 + i * 32; list[i].height = 480; }
    REQUIRE(DisplayModes_Select(list, n, DISPLAY_MODES_GAME_MAX, 1, 1) == DISPLAY_MODES_GAME_MAX);

    printf("display_modes: OK\n");
    return 0;
}
