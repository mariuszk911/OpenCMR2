/*
 * Bink decode benchmark for AmigaOS. Not staged; run from a Shell:
 *
 *     amiga_bink_bench FILE [TRACK [FRAMES]] > bink-bench.log
 *
 * Decodes FILE sequentially and prints one summary per pass: wall time and
 * per-frame average/max microseconds, plus the same video and PCM FNV64 hashes
 * as tests/bink_decode (docs/VIDEO.md, "Reference hashes"). The hashes must
 * equal the host's: that is the big-endian/GCC 6.5 correctness proof.
 *
 * Pass 1 decodes with the silent track (-1): packet read + video + BGRX
 * conversion. Pass 2 uses TRACK (default 0): the same plus audio, so
 * pass 2 - pass 1 is the audio cost. File read time is measured inside the
 * reader callback. The BGRX conversion is still inside Decoder::Decode until
 * the plane API exists, so it is also timed on its own over synthetic planes
 * with the decoder's exact formula (a cost estimate, not a measurement of the
 * decoder's loop). An emulator run is emulator evidence, not PiStorm evidence.
 *
 * Domain B: no game layout, no MiniGL. Hashes print as two 32-bit halves
 * because libnix printf has no reliable %llx.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"
#include "video/bink_decoder.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

struct FileReader {
    FILE *file;
    double readMs;
};

bool ReadAt(void *context, uint32_t offset, void *dst, uint32_t bytes)
{
    FileReader *reader = static_cast<FileReader *>(context);
    const double start = Sys_GetTicksPrecise();
    const bool ok = fseek(reader->file, long(offset), SEEK_SET) == 0 &&
                    fread(dst, 1, bytes, reader->file) == bytes;
    reader->readMs += Sys_GetTicksPrecise() - start;
    return ok;
}

const uint64_t kFnvBasis = 14695981039346656037ull;
const uint64_t kFnvPrime = 1099511628211ull;

struct PassResult {
    bool ok = false;
    unsigned frames = 0;
    unsigned long samples = 0;
    uint64_t videoHash = kFnvBasis, audioHash = kFnvBasis;
    double wallMs = 0, readMs = 0, maxFrameMs = 0;
};

// Decodes up to `limit` frames of `track`. Hashing is excluded from the
// per-frame times: the clock stops before it and the hash loop is timed apart.
PassResult RunPass(const char *path, int track, unsigned limit, bool hash)
{
    PassResult result;
    FileReader reader = {fopen(path, "rb"), 0};
    if (!reader.file) {
        printf("bink_bench: FAIL cannot open %s\n", path);
        return result;
    }
    long size = fseek(reader.file, 0, SEEK_END) == 0 ? ftell(reader.file) : -1;
    Bink::Decoder decoder;
    if (size < 0 || size > 0x7fffffff ||
        !decoder.Open({&reader, uint32_t(size), ReadAt}, track)) {
        printf("bink_bench: FAIL open: %s\n", decoder.Error().c_str());
        fclose(reader.file);
        return result;
    }
    const Bink::Info info = decoder.GetInfo();
    const unsigned count = limit && limit < info.frames ? limit : info.frames;
    Bink::Frame frame;
    reader.readMs = 0;
    double hashMs = 0;
    const double passStart = Sys_GetTicksPrecise();
    for (unsigned i = 0; i < count; ++i) {
        const double start = Sys_GetTicksPrecise();
        if (!decoder.Decode(frame)) {
            printf("bink_bench: FAIL frame %u: %s\n", i + 1, decoder.Error().c_str());
            fclose(reader.file);
            return result;
        }
        const double frameMs = Sys_GetTicksPrecise() - start;
        if (frameMs > result.maxFrameMs)
            result.maxFrameMs = frameMs;
        if (hash) {
            const double hashStart = Sys_GetTicksPrecise();
            for (size_t b = 0; b < frame.bgrx.size(); ++b) {
                result.videoHash ^= frame.bgrx[b];
                result.videoHash *= kFnvPrime;
            }
            for (size_t s = 0; s < frame.audio.size(); ++s) {
                // PCM oracle: IEEE-754 bits as four little-endian bytes. The
                // value is shifted, never reinterpreted through a pointer.
                uint32_t bits;
                memcpy(&bits, &frame.audio[s], sizeof(bits));
                for (int shift = 0; shift < 32; shift += 8) {
                    result.audioHash ^= (bits >> shift) & 0xff;
                    result.audioHash *= kFnvPrime;
                }
            }
            hashMs += Sys_GetTicksPrecise() - hashStart;
        }
        result.samples += (unsigned long)frame.audio.size();
    }
    result.wallMs = Sys_GetTicksPrecise() - passStart - hashMs;
    result.readMs = reader.readMs;
    result.frames = count;
    result.ok = true;
    fclose(reader.file);
    return result;
}

void PrintPass(const char *label, const PassResult &r)
{
    const unsigned long avgUs = r.frames ? (unsigned long)(r.wallMs * 1000.0 / r.frames) : 0;
    const unsigned long readUs = r.frames ? (unsigned long)(r.readMs * 1000.0 / r.frames) : 0;
    printf("bink_bench: %s: %u frames, wall %lu ms, avg %lu us/frame (read %lu us), max %lu us, "
           "%lu samples, video FNV64 %08lx%08lx, PCM FNV64 %08lx%08lx\n",
           label, r.frames, (unsigned long)r.wallMs, avgUs, readUs,
           (unsigned long)(r.maxFrameMs * 1000.0),
           r.samples,
           (unsigned long)(r.videoHash >> 32), (unsigned long)(r.videoHash & 0xffffffffu),
           (unsigned long)(r.audioHash >> 32), (unsigned long)(r.audioHash & 0xffffffffu));
}

uint8_t Clamp(int value)
{
    return uint8_t(value < 0 ? 0 : value > 255 ? 255 : value);
}

// The decoder's per-pixel YUV -> BGRX loop (bink_decoder.cpp, DecodeVideo) over
// a deterministic synthetic 640x480 frame, repeated.
void BenchConversion()
{
    const unsigned width = 640, height = 480, iterations = 10;
    std::vector<uint8_t> y(width * height), u(width * height / 4), v(width * height / 4);
    std::vector<uint8_t> out(width * height * 4);
    uint32_t seed = 0x1234567;
    for (size_t i = 0; i < y.size(); ++i) { seed = seed * 1664525 + 1013904223; y[i] = uint8_t(seed >> 24); }
    for (size_t i = 0; i < u.size(); ++i) {
        seed = seed * 1664525 + 1013904223; u[i] = uint8_t(seed >> 24);
        seed = seed * 1664525 + 1013904223; v[i] = uint8_t(seed >> 24);
    }
    uint32_t sum = 0;
    const double start = Sys_GetTicksPrecise();
    for (unsigned n = 0; n < iterations; ++n) {
        for (unsigned py = 0; py < height; ++py) {
            for (unsigned px = 0; px < width; ++px) {
                int luma = int(y[py * width + px]) - 16;
                if (luma < 0) luma = 0;
                int du = int(u[(py / 2) * (width / 2) + px / 2]) - 128;
                int dv = int(v[(py / 2) * (width / 2) + px / 2]) - 128;
                uint8_t *dst = &out[(size_t(py) * width + px) * 4];
                dst[0] = Clamp((298 * luma + 516 * du + 128) >> 8);
                dst[1] = Clamp((298 * luma - 100 * du - 208 * dv + 128) >> 8);
                dst[2] = Clamp((298 * luma + 409 * dv + 128) >> 8);
                dst[3] = 255;
            }
        }
        sum += out[n * 7];
    }
    const double ms = Sys_GetTicksPrecise() - start;
    printf("bink_bench: conversion estimate (synthetic 640x480, decoder formula): %lu us/frame (check %lu)\n",
           (unsigned long)(ms * 1000.0 / iterations), (unsigned long)sum);
}

} // namespace

int main(int argc, char **argv)
{
    AmigaSys_SetCommandLine("amiga_bink_bench");
    if (argc < 2) {
        printf("usage: amiga_bink_bench FILE [TRACK [FRAMES]]\n");
        return 5;
    }
    const int track = argc > 2 ? atoi(argv[2]) : 0;
    const unsigned limit = argc > 3 ? (unsigned)atoi(argv[3]) : 0;
    Sys_Log("bink_bench: begin %s track %d frames %u", argv[1], track, limit);

    PassResult silent = RunPass(argv[1], -1, limit, track < 0);
    if (!silent.ok)
        return 10;
    PrintPass("pass 1 (silent: read+video+convert)", silent);
    int status = 0;
    if (track >= 0) {
        PassResult audio = RunPass(argv[1], track, limit, true);
        if (!audio.ok)
            return 10;
        PrintPass("pass 2 (track: read+video+convert+audio)", audio);
        const double audioMs = audio.wallMs - silent.wallMs;
        printf("bink_bench: audio cost %ld ms over %u frames\n", (long)audioMs, audio.frames);
    }
    BenchConversion();
    printf("bink_bench: done (compare hashes with docs/VIDEO.md)\n");
    return status;
}
