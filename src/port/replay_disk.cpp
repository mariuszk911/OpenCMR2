#include "port/types.h"
#include "port/replay_disk.h"
#include "port/disk_layouts.h"
#include "port/endian.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdint.h>

namespace {
void EventToHost(ReplayEvent &event) {
    event.frame = (short)ReadLE16(&event.frame);
    event.count = (short)ReadLE16(&event.count);
}
void EventToDisk(unsigned char *dst, const ReplayEvent &event) {
    WriteLE16(dst, (uint16_t)event.frame);
    WriteLE16(dst + 2, (uint16_t)event.count);
    dst[4] = event.value;
    dst[5] = event.pad;
}
void IntsToHost(int *values, int count) {
    for (int i = 0; i < count; ++i)
        values[i] = (int)ReadLE32(&values[i]);
}
void IntsToDisk(unsigned char *dst, const int *values, int count) {
    for (int i = 0; i < count; ++i)
        WriteLE32(dst + i * (int)sizeof(int), (uint32_t)values[i]);
}
void SampleToHost(ReplaySample &sample) {
    sample.y = (int)ReadLE32(&sample.y);
    const uint32_t w4 = ReadLE32((unsigned char *)&sample + 4);
    const uint32_t w8 = ReadLE32((unsigned char *)&sample + 8);
    const uint32_t wc = ReadLE32((unsigned char *)&sample + 12);
    sample.x = (int)(int16_t)(w4 & 0xffff);
    sample.z = (int)(int16_t)(w4 >> 16);
    sample.sector = w8 & 0xffff;
    sample.rightAngles = w8 >> 16;
    sample.forwardHeading = wc & 0xff;
    sample.forwardPitch = (wc >> 8) & 0xff;
    sample.level = (wc >> 16) & 0x1f;
    sample.bits21 = (wc >> 21) & 0x7;
    sample.flag24 = (wc >> 24) & 0x1;
    sample.steering = (wc >> 25) & 0x1;
    sample.flag26 = (wc >> 26) & 0x1;
    sample.flag27 = (wc >> 27) & 0x1;
    sample.pad28 = (wc >> 28) & 0xf;
}
void SampleToDisk(unsigned char *dst, const ReplaySample &sample) {
    const uint32_t w4 = (uint32_t)(uint16_t)sample.x | ((uint32_t)(uint16_t)sample.z << 16);
    const uint32_t w8 = (sample.sector & 0xffff) | ((sample.rightAngles & 0xffff) << 16);
    const uint32_t wc = (sample.forwardHeading & 0xff) |
                        ((sample.forwardPitch & 0xff) << 8) |
                        ((sample.level & 0x1f) << 16) |
                        ((sample.bits21 & 0x7) << 21) |
                        ((sample.flag24 & 0x1) << 24) |
                        ((sample.steering & 0x1) << 25) |
                        ((sample.flag26 & 0x1) << 26) |
                        ((sample.flag27 & 0x1) << 27) |
                        ((sample.pad28 & 0xf) << 28);
    WriteLE32(dst, (uint32_t)sample.y);
    WriteLE32(dst + 4, w4);
    WriteLE32(dst + 8, w8);
    WriteLE32(dst + 12, wc);
}
void PayloadToHost(ReplayStream &p) {
    if (p.type == 0) {
        for (int i = 0; i < p.laneCapacity; ++i) {
            IntsToHost(p.pInputs[i].raceRecord, 6);
            for (int j = 0; j < 10; ++j)
                EventToHost(p.pInputs[i].events[j]);
        }
    } else {
        for (int i = 0; i < p.laneCapacity; ++i) {
            IntsToHost(reinterpret_cast<int *>(p.pStates[i].data), 7);
            for (int j = 0; j < 10; ++j)
                EventToHost(p.pStates[i].events[j]);
        }
    }
    if (p.type == 2) {
        ReplaySample *samples = reinterpret_cast<ReplaySample *>(p.pSamples);
        const int count = p.laneCapacity * p.samplesPerLane;
        for (int i = 0; i < count; ++i)
            SampleToHost(samples[i]);
    }
    for (int i = 0; i < p.laneCapacity; ++i)
        p.pLaneSamples[i] = (short)ReadLE16(&p.pLaneSamples[i]);
}
void PayloadToDisk(const ReplayStream &p, unsigned char *payload) {
    const size_t record = p.type == 0 ? sizeof(ReplayInputLane) : sizeof(ReplayStateLane);
    for (int i = 0; i < p.laneCapacity; ++i) {
        unsigned char *lane = payload + size_t(i) * record;
        if (p.type == 0) {
            IntsToDisk(lane + offsetof(ReplayInputLane, raceRecord), p.pInputs[i].raceRecord, 6);
            for (int j = 0; j < 10; ++j)
                EventToDisk(lane + offsetof(ReplayInputLane, events) + size_t(j) * sizeof(ReplayEvent),
                            p.pInputs[i].events[j]);
        } else {
            IntsToDisk(lane + offsetof(ReplayStateLane, data),
                       reinterpret_cast<const int *>(p.pStates[i].data), 7);
            for (int j = 0; j < 10; ++j)
                EventToDisk(lane + offsetof(ReplayStateLane, events) + size_t(j) * sizeof(ReplayEvent),
                            p.pStates[i].events[j]);
        }
    }
    if (p.type == 2) {
        const ReplaySample *samples = reinterpret_cast<const ReplaySample *>(p.pSamples);
        unsigned char *sample = payload + size_t(p.laneCapacity) * record;
        const int count = p.laneCapacity * p.samplesPerLane;
        for (int i = 0; i < count; ++i)
            SampleToDisk(sample + size_t(i) * sizeof(ReplaySample), samples[i]);
    }
    unsigned char *lengths = payload + size_t(p.laneCapacity) * record +
                             size_t(p.laneCapacity) * size_t(p.samplesPerLane) *
                                 (p.type == 2 ? sizeof(ReplaySample) : 4);
    for (int i = 0; i < p.laneCapacity; ++i)
        WriteLE16(lengths + size_t(i) * sizeof(short), (uint16_t)p.pLaneSamples[i]);
}
bool PayloadSize(const ReplayStream &p, size_t &size) {
    if (p.type < 0 || p.type > 2 || p.laneCapacity <= 0 || p.samplesPerLane <= 0 ||
        p.laneCount < 0 || p.laneCount > p.laneCapacity)
        return false;
    const size_t record = p.type == 0 ? sizeof(ReplayInputLane) : sizeof(ReplayStateLane);
    const size_t sample = p.type == 2 ? sizeof(ReplaySample) : 4;
    const size_t lane = record + size_t(p.samplesPerLane) * sample + sizeof(short);
    if (size_t(p.laneCapacity) > (std::numeric_limits<size_t>::max() - sizeof(ReplayStream)) / lane)
        return false;
    size = size_t(p.laneCapacity) * lane;
    return true;
}
void Pointers(ReplayStream &p) {
    unsigned char *data = reinterpret_cast<unsigned char *>(&p) + sizeof(p);
    const size_t record = p.type == 0 ? sizeof(ReplayInputLane) : sizeof(ReplayStateLane);
    p.pInputs = p.type == 0 ? reinterpret_cast<ReplayInputLane *>(data) : nullptr;
    p.pStates = p.type != 0 ? reinterpret_cast<ReplayStateLane *>(data) : nullptr;
    data += size_t(p.laneCapacity) * record;
    p.pSamples = p.type == 2 ? data : nullptr;
    p.pFrames = p.type != 2 ? data : nullptr;
    data += size_t(p.laneCapacity) * size_t(p.samplesPerLane) * (p.type == 2 ? sizeof(ReplaySample) : 4);
    p.pLaneSamples = reinterpret_cast<short *>(data);
}
}

ReplayStream *ReplayDisk_Read(const void *bytes, size_t size) {
    if (!bytes || size < g_diskLayout_ReplayStream.diskSize) return nullptr;
    ReplayStream header{};
    Disk_Unpack(&header, bytes, &g_diskLayout_ReplayStream);
    size_t payload;
    if (!PayloadSize(header, payload) || size != g_diskLayout_ReplayStream.diskSize + payload) return nullptr;
    auto *p = static_cast<ReplayStream *>(calloc(1, sizeof(header) + payload));
    if (!p) return nullptr;
    *p = header;
    memcpy(reinterpret_cast<unsigned char *>(p) + sizeof(header),
           static_cast<const unsigned char *>(bytes) + g_diskLayout_ReplayStream.diskSize, payload);
    Pointers(*p);
    PayloadToHost(*p);
    for (int i = 0; i < p->laneCount; ++i) {
        if (p->pLaneSamples[i] < 0 || p->pLaneSamples[i] > p->samplesPerLane) {
            free(p); return nullptr;
        }
    }
    return p;
}

bool ReplayDisk_Write(const ReplayStream *p, std::vector<unsigned char> &bytes) {
    size_t payload;
    if (!p || !PayloadSize(*p, payload)) return false;
    bytes.resize(g_diskLayout_ReplayStream.diskSize + payload);
    Disk_Pack(bytes.data(), p, &g_diskLayout_ReplayStream);
    // Stored runtime array addresses are rebuilt after loading.
    for (int i = 0; i < g_diskLayout_ReplayStream.pointerCount; ++i) {
        const auto &field = g_diskLayout_ReplayStream.pointers[i];
        uint32_t value = 0;
        memcpy(bytes.data() + field.disk, &value, sizeof(value));
    }
    memcpy(bytes.data() + g_diskLayout_ReplayStream.diskSize,
           reinterpret_cast<const unsigned char *>(p) + sizeof(*p), payload);
    PayloadToDisk(*p, bytes.data() + g_diskLayout_ReplayStream.diskSize);
    return true;
}
