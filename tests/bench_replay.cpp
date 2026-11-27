#include "diagnostics/bench_replay.h"

#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    int country = -1, stage = -1;
    // The game's country order: FIN GRE FRA SWE AUS KEN ITA UK JAP.
    REQUIRE(BenchReplay_ParseStage("FIN01", &country, &stage) && country == 0 && stage == 0);
    REQUIRE(BenchReplay_ParseStage("ken03", &country, &stage) && country == 5 && stage == 2);
    REQUIRE(BenchReplay_ParseStage("Swe01", &country, &stage) && country == 3 && stage == 0);
    REQUIRE(BenchReplay_ParseStage("UK11", &country, &stage) && country == 7 && stage == 10);
    REQUIRE(BenchReplay_ParseStage("JAP01", &country, &stage) && country == 8 && stage == 0);

    country = stage = -1;
    REQUIRE(!BenchReplay_ParseStage(NULL, &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FIN", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FIN1", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FIN00", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FIN12", &country, &stage)); // 11 stages per country in the game's tables
    REQUIRE(!BenchReplay_ParseStage("FIN33", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FIN011", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("USA01", &country, &stage));
    REQUIRE(!BenchReplay_ParseStage("FINLAND01", &country, &stage));
    REQUIRE(country == -1 && stage == -1);

    // The attract drives the first selection of the run's car type.
    const int types[] = { 0, 0, 0, 0, 1, 1, 6, 4 };
    REQUIRE(BenchReplay_SelectionForType(0, types, 8) == 0);
    REQUIRE(BenchReplay_SelectionForType(1, types, 8) == 4);
    REQUIRE(BenchReplay_SelectionForType(4, types, 8) == 7);
    REQUIRE(BenchReplay_SelectionForType(2, types, 8) == -1);
    REQUIRE(BenchReplay_SelectionForType(-1, types, 8) == -1);
    REQUIRE(BenchReplay_SelectionForType(0, types, 0) == -1);
    return 0;
}
