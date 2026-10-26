#include "port/types.h"
#include "port/replay_disk.h"
#include "port/disk_layouts.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

extern "C" void Sys_Log(const char *, ...) {}

namespace {

unsigned long RawLE32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
           (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

unsigned RawLE16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

struct Fixture {
    std::vector<unsigned char> storage;
    ReplayStream *stream;
    size_t payload;
    unsigned char *payloadPtr;
};

void FillEvents(ReplayEvent *events)
{
    for (int i = 0; i < 10; ++i) {
        events[i].frame = (short)(0x100 + i);
        events[i].count = (short)(0x200 + i);
        events[i].value = (BYTE)(0x30 + i);
        events[i].pad = (BYTE)(0x40 + i);
    }
}

void FillInts(int *values, int count, int base)
{
    for (int i = 0; i < count; ++i)
        values[i] = base + i;
}

Fixture Build(int type)
{
    const int laneCapacity = 2;
    const int samplesPerLane = 3;
    const size_t record = type == 0 ? sizeof(ReplayInputLane) : sizeof(ReplayStateLane);
    const size_t sample = type == 2 ? sizeof(ReplaySample) : 4;
    const size_t payload = laneCapacity * (record + samplesPerLane * sample + sizeof(short));
    Fixture f;
    f.payload = payload;
    f.storage.assign(sizeof(ReplayStream) + payload, 0);
    f.stream = reinterpret_cast<ReplayStream *>(f.storage.data());
    f.payloadPtr = f.storage.data() + sizeof(ReplayStream);
    ReplayStream &s = *f.stream;
    s.type = type;
    s.laneCapacity = laneCapacity;
    s.samplesPerLane = samplesPerLane;
    s.laneCount = 2;
    s.steeringModeOption = 0x11223344;
    s.headingFrom = -123;
    s.headingTo = 456;
    s.field_0x10c = 8;

    if (type == 0) {
        s.pInputs = reinterpret_cast<ReplayInputLane *>(f.payloadPtr);
        s.pFrames = f.payloadPtr + laneCapacity * record;
        s.pLaneSamples = reinterpret_cast<short *>(f.payloadPtr + laneCapacity * (record + samplesPerLane * sample));
        for (int lane = 0; lane < laneCapacity; ++lane) {
            for (int i = 0; i < (int)sizeof(s.pInputs[lane].partState); ++i)
                s.pInputs[lane].partState[i] = (BYTE)(lane * 17 + i);
            for (int i = 0; i < (int)sizeof(s.pInputs[lane].carState); ++i)
                s.pInputs[lane].carState[i] = (BYTE)(lane * 29 + i);
            FillInts(s.pInputs[lane].raceRecord, 6, 0x01020304 + lane * 10);
            FillEvents(s.pInputs[lane].events);
            s.pInputs[lane].eventCount = 10;
        }
        for (size_t i = 0; i < laneCapacity * samplesPerLane * sample; ++i)
            s.pFrames[i] = (BYTE)(0x50 + i);
    } else {
        s.pStates = reinterpret_cast<ReplayStateLane *>(f.payloadPtr);
        s.pFrames = f.payloadPtr + laneCapacity * record;
        if (type == 2) {
            s.pFrames = NULL;
            s.pSamples = f.payloadPtr + laneCapacity * record;
            s.pLaneSamples = reinterpret_cast<short *>(f.payloadPtr + laneCapacity * record +
                                                      laneCapacity * samplesPerLane * sizeof(ReplaySample));
        } else {
            s.pLaneSamples = reinterpret_cast<short *>(f.payloadPtr + laneCapacity * (record + samplesPerLane * sample));
        }
        for (int lane = 0; lane < laneCapacity; ++lane) {
            FillInts(reinterpret_cast<int *>(s.pStates[lane].data), 7, 0x01020304 + lane * 10);
            FillEvents(s.pStates[lane].events);
            s.pStates[lane].eventCount = 10;
        }
        if (type == 1) {
            for (size_t i = 0; i < laneCapacity * samplesPerLane * sample; ++i)
                s.pFrames[i] = (BYTE)(0x50 + i);
        } else {
            ReplaySample *samples = reinterpret_cast<ReplaySample *>(s.pSamples);
            for (int i = 0; i < laneCapacity * samplesPerLane; ++i) {
                samples[i].y = 0x01020304 + i;
                samples[i].x = (int)(-1000 + i);
                samples[i].z = (int)(2000 + i);
                samples[i].sector = (unsigned)(3000 + i);
                samples[i].rightAngles = (unsigned)(4000 + i);
                samples[i].forwardHeading = (unsigned)(0x10 + i);
                samples[i].forwardPitch = (unsigned)(0x20 + i);
                samples[i].level = (unsigned)(i & 0x1f);
                samples[i].bits21 = (unsigned)(i & 0x7);
                samples[i].flag24 = (unsigned)(i & 1);
                samples[i].steering = (unsigned)((i + 1) & 1);
                samples[i].flag26 = (unsigned)((i + 2) & 1);
                samples[i].flag27 = (unsigned)((i + 3) & 1);
                samples[i].pad28 = (unsigned)(i & 0xf);
            }
        }
    }

    s.pLaneSamples[0] = 3;
    s.pLaneSamples[1] = 1;
    return f;
}

int CompareStreams(const ReplayStream &a, const ReplayStream &b)
{
    REQUIRE(a.type == b.type && a.laneCapacity == b.laneCapacity &&
            a.samplesPerLane == b.samplesPerLane && a.laneCount == b.laneCount);
    REQUIRE(a.steeringModeOption == b.steeringModeOption);
    REQUIRE(a.headingFrom == b.headingFrom && a.headingTo == b.headingTo &&
            a.field_0x10c == b.field_0x10c);
    REQUIRE(a.pLaneSamples[0] == b.pLaneSamples[0] && a.pLaneSamples[1] == b.pLaneSamples[1]);
    if (a.type == 0) {
        for (int lane = 0; lane < a.laneCapacity; ++lane) {
            REQUIRE(!memcmp(a.pInputs[lane].partState, b.pInputs[lane].partState,
                            sizeof(a.pInputs[lane].partState)));
            REQUIRE(!memcmp(a.pInputs[lane].carState, b.pInputs[lane].carState,
                            sizeof(a.pInputs[lane].carState)));
            REQUIRE(!memcmp(a.pInputs[lane].raceRecord, b.pInputs[lane].raceRecord,
                            sizeof(a.pInputs[lane].raceRecord)));
            REQUIRE(!memcmp(a.pInputs[lane].events, b.pInputs[lane].events,
                            sizeof(a.pInputs[lane].events)));
        }
        REQUIRE(!memcmp(a.pFrames, b.pFrames, size_t(a.laneCapacity) * a.samplesPerLane * 4));
    } else {
        for (int lane = 0; lane < a.laneCapacity; ++lane) {
            REQUIRE(!memcmp(a.pStates[lane].data, b.pStates[lane].data, sizeof(a.pStates[lane].data)));
            REQUIRE(!memcmp(a.pStates[lane].events, b.pStates[lane].events,
                            sizeof(a.pStates[lane].events)));
        }
        if (a.type == 1) {
            REQUIRE(!memcmp(a.pFrames, b.pFrames, size_t(a.laneCapacity) * a.samplesPerLane * 4));
        } else {
            const int count = a.laneCapacity * a.samplesPerLane;
            const ReplaySample *sa = reinterpret_cast<const ReplaySample *>(a.pSamples);
            const ReplaySample *sb = reinterpret_cast<const ReplaySample *>(b.pSamples);
            for (int i = 0; i < count; ++i) {
                REQUIRE(sa[i].y == sb[i].y && sa[i].x == sb[i].x && sa[i].z == sb[i].z);
                REQUIRE(sa[i].sector == sb[i].sector && sa[i].rightAngles == sb[i].rightAngles);
                REQUIRE(sa[i].forwardHeading == sb[i].forwardHeading &&
                        sa[i].forwardPitch == sb[i].forwardPitch);
                REQUIRE(sa[i].level == sb[i].level && sa[i].bits21 == sb[i].bits21);
                REQUIRE(sa[i].flag24 == sb[i].flag24 && sa[i].steering == sb[i].steering);
                REQUIRE(sa[i].flag26 == sb[i].flag26 && sa[i].flag27 == sb[i].flag27);
                REQUIRE(sa[i].pad28 == sb[i].pad28);
            }
        }
    }
    return 0;
}

} // namespace

int main()
{
    for (int type = 0; type < 3; ++type) {
        Fixture f = Build(type);
        std::vector<unsigned char> file;
        REQUIRE(ReplayDisk_Write(f.stream, file));
        REQUIRE(file.size() == g_diskLayout_ReplayStream.diskSize + f.payload);

#if !defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN)
        REQUIRE(RawLE32(file.data() + 0x1c) == (unsigned long)type);
        REQUIRE(RawLE16(file.data() + 0xfc) == 2);
        if (type == 0) {
            REQUIRE(RawLE32(file.data() + g_diskLayout_ReplayStream.diskSize +
                            offsetof(ReplayInputLane, raceRecord)) == 0x01020304ul);
            REQUIRE(RawLE16(file.data() + g_diskLayout_ReplayStream.diskSize +
                            offsetof(ReplayInputLane, events) + 2) == 0x200);
        } else {
            REQUIRE(RawLE32(file.data() + g_diskLayout_ReplayStream.diskSize +
                            offsetof(ReplayStateLane, data)) == 0x01020304ul);
            REQUIRE(RawLE16(file.data() + g_diskLayout_ReplayStream.diskSize +
                            offsetof(ReplayStateLane, events) + 2) == 0x200);
        }
        if (type == 2) {
            REQUIRE(RawLE32(file.data() + g_diskLayout_ReplayStream.diskSize +
                            sizeof(ReplayStateLane) * 2) == 0x01020304ul);
        }
        REQUIRE(RawLE16(file.data() + file.size() - 4) == 3);
#endif

        ReplayStream *read = ReplayDisk_Read(file.data(), file.size());
        REQUIRE(read != NULL);
        REQUIRE(CompareStreams(*f.stream, *read) == 0);
        std::vector<unsigned char> rewritten;
        REQUIRE(ReplayDisk_Write(read, rewritten));
        REQUIRE(rewritten == file);
        free(read);
    }
    std::printf("replay payload: OK\n");
    return 0;
}
