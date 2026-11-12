// Per-tick trace of car 0, see diagnostics/replay_trace.h.

#include "diagnostics/replay_trace.h"

#include "port/diagnostics.h"
#include "port/sys.h"
#include "Car.h"
#include "ReplayStream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// StageObjects.cpp: the sixteen replay slots the playback loop walks.
extern ReplayStream **g_replaySlots[16];

namespace Diagnostics {
namespace {

const char *const kFieldNames[] = {
    "tick", "state", "playing", "lane", "frame", "counter",
    "in0", "in1", "in2", "in3", "handbrake", "analogThrottle", "analogBrake", "steerMode",
    "px", "py", "pz", "vx", "vy", "vz",
    "rx", "ry", "rz", "ux", "uy", "uz", "fx", "fy", "fz",
    "speed", "steerAngle", "gear", "engineSpeed",
    "surf0", "surf1", "surf2", "surf3", "surfType0", "surfType1", "surfType2", "surfType3",
    "spring0", "spring1", "spring2", "spring3", "stepsLeft",
};
enum { kFields = sizeof(kFieldNames) / sizeof(kFieldNames[0]), kMaxTicks = 24000 };

// The wheel spring offsets (Car +0x9d8) were wheel0x9d8 before upstream 1.8.7
// named them; this picks whichever name the Car of the commit has, so the
// same trace builds on both sides of that merge.
template <class T> auto WheelSpring(T *c, int i, int) -> decltype(c->wheelSpringOffset[0]) { return c->wheelSpringOffset[i]; }
template <class T> auto WheelSpring(T *c, int i, long) -> decltype(c->wheel0x9d8[0]) { return c->wheel0x9d8[i]; }

// The playing stream of car 0, or NULL.
ReplayStream *FindCar0Stream()
{
    for (int i = 0; i < 16; i++) {
        if (g_replaySlots[i] == NULL)
            continue;
        ReplayStream *p = *g_replaySlots[i];
        if (p != NULL && p->car == 0 && p->playing != 0)
            return p;
    }
    return NULL;
}

struct ReplayTrace : Observer {
    int *records;
    unsigned count;
    unsigned tick;
    unsigned char state;
    int wasPlaying;
    int written;
    char path[256];

    ReplayTrace() : records(NULL), count(0), tick(0), state(0), wasPlaying(0), written(0) { path[0] = 0; }

    void BeginTick(unsigned char s) override { state = s; }

    void EndTick() override
    {
        ReplayStream *stream = FindCar0Stream();
        int playing = stream != NULL;

        if (count < kMaxTicks) {
            Car *c = Car_Get(0);
            int *r = records + (size_t)count * kFields;
            int n = 0;
            r[n++] = (int)tick;
            r[n++] = state;
            r[n++] = playing;
            r[n++] = stream ? stream->lane : -1;
            r[n++] = stream ? stream->frame : -1;
            r[n++] = stream ? stream->field_0x21 : -1;
            for (int i = 0; i < 4; i++)
                r[n++] = (unsigned char)c->flag0x1d0[i];
            r[n++] = c->handbrake;
            r[n++] = c->analogThrottle;
            r[n++] = c->analogBrake;
            r[n++] = c->steeringInputMode;
            r[n++] = c->position.x; r[n++] = c->position.y; r[n++] = c->position.z;
            r[n++] = c->velocity.x; r[n++] = c->velocity.y; r[n++] = c->velocity.z;
            r[n++] = c->right.x; r[n++] = c->right.y; r[n++] = c->right.z;
            r[n++] = c->up.x; r[n++] = c->up.y; r[n++] = c->up.z;
            r[n++] = c->forward.x; r[n++] = c->forward.y; r[n++] = c->forward.z;
            r[n++] = c->speed;
            r[n++] = c->wheelSteeringAngle;
            r[n++] = c->gear;
            r[n++] = c->engineSpeed;
            for (int i = 0; i < 4; i++)
                r[n++] = c->wheelSurface[i];
            for (int i = 0; i < 4; i++)
                r[n++] = c->wheelSurfaceType[i];
            for (int i = 0; i < 4; i++)
                r[n++] = WheelSpring(c, i, 0);
            r[n++] = c->simulationStepsRemaining;
            count++;
        }
        tick++;
        // The replay of car 0 ended, or the buffer is full: write once.
        if ((wasPlaying && !playing) || count == kMaxTicks)
            Write();
        wasPlaying = playing;
    }

    void Write()
    {
        if (count == 0 || path[0] == 0)
            return;
        // The first write creates the file with its header; later ones (a
        // second replay, the exit) append to it.
        FILE *file = fopen(path, written ? "a" : "w");
        if (file == NULL) {
            Sys_Log("replay trace: cannot write %s", path);
            return;
        }
        if (!written) {
            for (int f = 0; f < kFields; f++)
                fprintf(file, "%s%s", f ? "," : "", kFieldNames[f]);
            fputc('\n', file);
        }
        written = 1;
        for (unsigned t = 0; t < count; t++) {
            const int *r = records + (size_t)t * kFields;
            for (int f = 0; f < kFields; f++)
                fprintf(file, "%s%d", f ? "," : "", r[f]);
            fputc('\n', file);
        }
        fclose(file);
        Sys_Log("replay trace: wrote %u ticks to %s", count, path);
        count = 0;
    }
};

ReplayTrace *s_trace;

} // namespace

void ReplayTrace_StartFromOptions()
{
    const char *path = Sys_GetOptionText("debug.replay_trace", NULL);
    if (path == NULL || *path == 0 || observer != NULL || s_trace != NULL)
        return;
    ReplayTrace *trace = new ReplayTrace;
    trace->records = (int *)malloc((size_t)kMaxTicks * kFields * sizeof(int));
    if (trace->records == NULL) {
        delete trace;
        Sys_Log("replay trace: no memory");
        return;
    }
    strncpy(trace->path, path, sizeof(trace->path) - 1);
    s_trace = trace;
    observer = trace;
    Sys_Log("replay trace: enabled, %s (%d fields, up to %d ticks)", trace->path, (int)kFields, (int)kMaxTicks);
}

void ReplayTrace_Stop()
{
    if (s_trace == NULL)
        return;
    s_trace->Write();
    if (observer == s_trace)
        observer = NULL;
    free(s_trace->records);
    delete s_trace;
    s_trace = NULL;
}

} // namespace Diagnostics
