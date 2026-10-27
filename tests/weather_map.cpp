#include "port/weather_map.h"

#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    /* Every stage residue and both preview panels stay inside the maps the
       loader filled for the current rally group. */
    for (int stage = 0; stage <= 31; ++stage) {
        for (int panel = 0; panel < 2; ++panel) {
            for (int count = 2; count <= 4; ++count) {
                const int index = WeatherMap_Index(stage, panel, count);
                REQUIRE(index >= 0);
                REQUIRE(index < count);
            }
        }
        for (int count = 2; count <= 4; ++count) {
            /* The panel stage indices form a range, so they must not go
               backwards at a group boundary. */
            REQUIRE(WeatherMap_Index(stage, 1, count) >= WeatherMap_Index(stage, 0, count));
        }
    }

    /* Panels stay in stage order; a group boundary repeats the last loaded
       map instead of wrapping to the first one. */
    REQUIRE(WeatherMap_Index(0, 0, 4) == 0);
    REQUIRE(WeatherMap_Index(0, 1, 4) == 1);
    REQUIRE(WeatherMap_Index(2, 1, 3) == 2);
    REQUIRE(WeatherMap_Index(3, 0, 4) == 3);
    REQUIRE(WeatherMap_Index(3, 1, 4) == 3);
    REQUIRE(WeatherMap_Index(6, 1, 3) == 2);
    REQUIRE(WeatherMap_Index(7, 1, 4) == 3);
    REQUIRE(WeatherMap_Index(8, 0, 2) == 0);
    REQUIRE(WeatherMap_Index(9, 1, 2) == 1);
    REQUIRE(WeatherMap_Index(10, 0, 3) == 2);

    /* Defensive bounds for values the loader cannot produce. */
    REQUIRE(WeatherMap_Index(0, 0, 0) == 0);
    REQUIRE(WeatherMap_Index(3, 1, 9) == 3);

    printf("weather_map: OK\n");
    return 0;
}
