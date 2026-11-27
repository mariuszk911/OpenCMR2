// Record-a-run and play-it-as-attract benchmark, see diagnostics/bench_replay.h.

#include "diagnostics/bench_replay.h"

#include "port/sys.h"
#include "port/replay_disk.h"
#include "FileBuffer.h"
#include "Frontend.h"
#include "GameInfo.h"
#include "GenericFileLoader.h"
#include "InstallInfo.h"
#include "ReplayStream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Replay_Save(BYTE *pBuffer, char *pName);
char *Race_GetStagePathBuffer38444(void);
extern char *g_unk0x0051931c[9];  // the country codes (FIN..JAP) of the stage file names
extern char g_str0x0051942c[];    // "%s\Game\DemoRuns\%s%.2d", the game's DemoRuns path
extern char g_str0x005192dc[];    // "%sH.rpl"
extern char g_str0x005192d4[];    // "%sL.rpl"

// Offset of Car::type (game/Car.h, 0xb1b) in the car state a type 0 lane
// starts from: the original 32-bit Car layout, as the replay file stores it
// (port/replay_disk.cpp decodes the lanes, not this block).
static const int kLaneCarTypeOffset = 0xb1b;
// Car selections the frontend offers (CFrontend::GetArchivePrimaryIDEntry).
static const int kCarSelections = 22;

namespace {

struct AttractRun {
    int country = 0, stage = 0, selection = 0;
};

// The run [debug] attract_stage names: its stage, and the car selection its
// DemoRuns file was driven with. Resolved again only when the key or Track
// Detail changes, so the file is read and the outcome logged once.
int ResolveAttractRun(AttractRun *run)
{
    static char lastText[32];
    static int lastHigh = -1, lastOk;
    static AttractRun last;

    if (!BenchReplay_Enabled())
        return 0;
    const char *text = Sys_GetOptionText("debug.attract_stage", NULL);
    if (text == NULL || *text == 0)
        return 0;
    if (strlen(text) >= sizeof(lastText)) {
        static int loggedLong;
        if (!loggedLong)
            Sys_Log("bench replay: attract_stage is not a stage (FIN01..JAP11)");
        loggedLong = 1;
        return 0;
    }
    const int high = CGameInfo::GetGraphicsOptionBits27To28() == 0;
    if (high == lastHigh && strncmp(text, lastText, sizeof(lastText)) == 0) {
        *run = last;
        return lastOk;
    }
    snprintf(lastText, sizeof(lastText), "%s", text);
    lastHigh = high;
    lastOk = 0;
    last = AttractRun();

    AttractRun found;
    if (!BenchReplay_ParseStage(text, &found.country, &found.stage)) {
        Sys_Log("bench replay: attract_stage '%s' is not a stage (FIN01..JAP11)", text);
        return 0;
    }
    // The attract's demo file, named as Replay_LoadSelectedRun names it.
    char stem[300], path[310];
    snprintf(stem, sizeof(stem), g_str0x0051942c, CInstallInfo::GetGameHDPath(), g_unk0x0051931c[found.country],
             found.stage + 1);
    snprintf(path, sizeof(path), high ? g_str0x005192dc : g_str0x005192d4, stem);
    void *file = CFileBuffer::GetGenericFileBuffer(path, 1);
    if (file == NULL) {
        Sys_Log("bench replay: %s not found; the attract keeps its stages", path);
        return 0;
    }
    ReplayStream *stream = ReplayDisk_Read(file, (size_t)CGenericFileLoader::GetGenericFileSize());
    CFileBuffer::FreeGenericFileBuffer(file);
    int type = -1;
    if (stream != NULL && stream->type == 0 && stream->laneCount > 0)
        type = (signed char)stream->pInputs[0].carState[kLaneCarTypeOffset];
    free(stream); // ReplayDisk_Read's own buffer (calloc)
    int typeOfSelection[kCarSelections];
    for (int i = 0; i < kCarSelections; i++)
        typeOfSelection[i] = CFrontend::GetArchivePrimaryIDEntry(i);
    found.selection = BenchReplay_SelectionForType(type, typeOfSelection, kCarSelections);
    if (found.selection < 0) {
        Sys_Log("bench replay: %s is not a recorded run of a known car (type %d); the attract keeps its stages",
                path, type);
        return 0;
    }
    Sys_Log("bench replay: attract plays %s (car type %d, selection %d)", path, type, found.selection);
    last = found;
    lastOk = 1;
    *run = found;
    return 1;
}

} // namespace

int BenchReplay_Enabled(void)
{
    return Sys_GetOption("debug.attract_benchmark", 0) != 0;
}

int BenchReplay_AttractStage(int *country, int *stage)
{
    AttractRun run;
    if (!ResolveAttractRun(&run))
        return 0;
    *country = run.country;
    *stage = run.stage;
    return 1;
}

int BenchReplay_AttractCar(void)
{
    AttractRun run;
    return ResolveAttractRun(&run) ? run.selection : 0;
}

void BenchReplay_SaveFinishedRun(int player, uint8_t *stream)
{
    if (stream == NULL || player != 0 || !BenchReplay_UseAttractSeed())
        return;
    char path[300];
    snprintf(path, sizeof(path), "%s.rpl", Race_GetStagePathBuffer38444());
    // The game's Replay_Save does not report a failed write.
    Replay_Save(stream, path);
    Sys_Log("bench replay: run written to %s", path);
}

int BenchReplay_UseAttractSeed(void)
{
    return BenchReplay_Enabled() && Sys_GetOption("debug.save_replay", 0) != 0;
}
