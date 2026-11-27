/* A fixed workload for benchmarking any stage: record a run, then play it as
 * the attract demo. Domain A: it calls the game's replay and path functions.
 *
 * Everything here, and the bench timing of the Amiga renderers
 * (render/gfx_dispatch_amiga.cpp), acts only with [debug] attract_benchmark =
 * 1; without it (the default) the game behaves as released.
 *
 * [debug] save_replay = 1 writes player 1's run when its recording stops
 * after the finish, to "<install>\replays\<stage><H|L>.rpl" (FIN01H.rpl for
 * Finland stage 1; H or L as the game names its DemoRuns files from GameInfo
 * graphics bits 27-28), replacing the previous one. Copy that file to
 * Game\DemoRuns\ to play it (the game's writer does not report a failed
 * write). It also starts every race with the attract's
 * random seed (0 instead of the clock): the physics draws random numbers
 * (surfaces, landings), so a run recorded with another seed leaves the road
 * when the attract plays it.
 *
 * [debug] attract_stage = FIN01 makes every attract demo that stage instead
 * of the Kenya/Sweden/France rotation; the demo plays
 * Game\DemoRuns\<stage><H|L>.rpl with the car model the run was driven with.
 * The codes are FIN GRE FRA SWE AUS KEN ITA UK JAP, the number is the stage
 * (01..11). A missing or unreadable file keeps the original rotation (logged).
 *
 * Recording conditions for a run that replays on the road: a single-player
 * race in the mode the attract uses, the same Track Detail (H/L), and the
 * base car of its type (the replay file stores the car type, not the
 * selection; the attract drives the first selection of that type). */
#ifndef OPENCMR2_DIAGNOSTICS_BENCH_REPLAY_H
#define OPENCMR2_DIAGNOSTICS_BENCH_REPLAY_H

#include <stdint.h>

/* Stages per country in the game's tables (indexed country * 0xb + stage). */
#define BENCH_REPLAY_MAX_STAGES 11

/* Parses "FIN01"-style text, any case: country index (FIN = 0 .. JAP = 8, the
 * order of the game's country codes) and stage index (01 -> 0, up to 11).
 * Returns 0 for anything else and then leaves the outputs alone. Pure,
 * host-tested (tests/bench_replay.cpp). */
static inline int BenchReplay_ParseStage(const char *text, int *country, int *stage)
{
    static const char codes[9][4] = { "FIN", "GRE", "FRA", "SWE", "AUS", "KEN", "ITA", "UK", "JAP" };

    if (text == 0)
        return 0;
    for (int c = 0; c < 9; c++) {
        const char *code = codes[c];
        const char *p = text;
        while (*code != 0 && (*p | 0x20) == (*code | 0x20)) {
            code++;
            p++;
        }
        if (*code != 0)
            continue;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != 0)
            return 0;
        int number = (p[0] - '0') * 10 + (p[1] - '0');
        if (number < 1 || number > BENCH_REPLAY_MAX_STAGES)
            return 0;
        *country = c;
        *stage = number - 1;
        return 1;
    }
    return 0;
}

/* The first car selection whose model is `type`, given each selection's
 * model type; -1 when none is. Pure, host-tested. */
static inline int BenchReplay_SelectionForType(int type, const int *typeOfSelection, int count)
{
    for (int selection = 0; selection < count; selection++)
        if (typeOfSelection[selection] == type)
            return selection;
    return -1;
}

/* Whether [debug] attract_benchmark = 1 turns the benchmark on. */
int BenchReplay_Enabled(void);

/* The stage [debug] attract_stage selects. Returns 0 when the benchmark is
 * off, the key is unset or not a stage, or its DemoRuns file cannot be read
 * as a replay of a known car (logged once per setting). */
int BenchReplay_AttractStage(int *country, int *stage);

/* The car selection the attract drives: the model the [debug] attract_stage
 * run was recorded with, or 0, the original's car, when there is none. */
int BenchReplay_AttractCar(void);

/* Saves `stream`, whose recording just stopped after the finish, when the
 * benchmark is on with [debug] save_replay and it is player 1's (`player`
 * 0). */
void BenchReplay_SaveFinishedRun(int player, uint8_t *stream);

/* Whether a race starts with the attract's random seed: [debug] save_replay
 * with the benchmark on. */
int BenchReplay_UseAttractSeed(void);

#endif
