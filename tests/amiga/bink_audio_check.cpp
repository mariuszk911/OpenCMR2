/*
 * Audio check for the 68k target. The target's float math differs from the
 * host's in the last bits, so the decoded PCM is compared with a host-made
 * reference by tolerance instead of by hash:
 *
 *     bink_decode FILE 0 "" REF.f32 FRAMES        (host, little-endian float32)
 *     amiga_bink_audio_check FILE REF.f32 [FRAMES [planes]]
 *
 * With "planes" it decodes as the game does (Decode(frame, &planes): no BGRX
 * conversion) and also checks that every frame's sample count is sane.
 * Reports the largest absolute difference, the number of samples that differ
 * by more than 1e-5 and the time per frame of Decode (video, BGRX conversion and
 * audio together). Exit status 10 on failure.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"
#include "video/bink_decoder.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
bool ReadAt(void *context, uint32_t offset, void *dst, uint32_t bytes)
{
    FILE *file = static_cast<FILE *>(context);
    return fseek(file, long(offset), SEEK_SET) == 0 && fread(dst, 1, bytes, file) == bytes;
}

// IEEE-754 single from four little-endian bytes, assembled explicitly.
float LittleEndianFloat(const uint8_t *b)
{
    const uint32_t bits = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
} // namespace

int main(int argc, char **argv)
{
    AmigaSys_SetCommandLine("amiga_bink_audio_check");
    if (argc < 3) {
        printf("usage: amiga_bink_audio_check FILE REF.f32 [FRAMES [planes]]\n");
        return 5;
    }
    FILE *movie = fopen(argv[1], "rb");
    FILE *reference = fopen(argv[2], "rb");
    int status = 10;
    if (!movie || !reference) {
        printf("audio_check: FAIL cannot open %s\n", movie ? argv[2] : argv[1]);
    } else {
        long size = fseek(movie, 0, SEEK_END) == 0 ? ftell(movie) : -1;
        Bink::Decoder decoder;
        if (size > 0 && size <= 0x7fffffff && decoder.Open({movie, uint32_t(size), ReadAt}, 0)) {
            const Bink::Info info = decoder.GetInfo();
            const unsigned limit = argc > 3 ? (unsigned)atoi(argv[3]) : 0;
            const unsigned count = limit && limit < info.frames ? limit : info.frames;
            const bool usePlanes = argc > 4 && strcmp(argv[4], "planes") == 0;
            Bink::Frame frame;
            Bink::Planes planes = {};
            double maxDiff = 0, decodeMs = 0;
            unsigned long samples = 0, over = 0;
            status = 0;
            for (unsigned i = 0; i < count && status == 0; ++i) {
                const double start = Sys_GetTicksPrecise();
                if (!(usePlanes ? decoder.Decode(frame, &planes) : decoder.Decode(frame))) {
                    printf("audio_check: FAIL frame %u: %s\n", i + 1, decoder.Error().c_str());
                    status = 10;
                    break;
                }
                decodeMs += Sys_GetTicksPrecise() - start;
                if (frame.audio.size() > 8u * 1024u * 1024u || frame.bgrx.size() != (usePlanes ? 0u : size_t(info.width) * info.height * 4)) {
                    printf("audio_check: FAIL frame %u: %lu samples, %lu BGRX bytes\n", i + 1,
                           (unsigned long)frame.audio.size(), (unsigned long)frame.bgrx.size());
                    status = 10;
                    break;
                }
                if (i < 3)
                    printf("audio_check: frame %u: %lu samples\n", i + 1, (unsigned long)frame.audio.size());
                for (size_t s = 0; s < frame.audio.size(); ++s) {
                    uint8_t raw[4];
                    if (fread(raw, 1, 4, reference) != 4) {
                        printf("audio_check: FAIL reference ends at sample %lu\n", samples);
                        status = 10;
                        break;
                    }
                    const double diff = fabs(double(frame.audio[s]) - double(LittleEndianFloat(raw)));
                    if (diff > maxDiff) maxDiff = diff;
                    if (diff > 1e-5) ++over;
                    ++samples;
                }
            }
            if (status == 0) {
                // The maximum is printed in units of 1e-9 (libnix printf has no %f).
                printf("audio_check: OK %u frames, %lu samples, max abs diff %lu e-9, %lu over 1e-5, "
                       "%lu us/frame (video + audio%s)\n", count, samples, (unsigned long)(maxDiff * 1e9), over,
                       (unsigned long)(decodeMs * 1000.0 / count), usePlanes ? ", planes" : ", BGRX");
                if (over != 0) status = 10;
            }
        } else {
            printf("audio_check: FAIL open: %s\n", decoder.Error().c_str());
        }
    }
    if (movie) fclose(movie);
    if (reference) fclose(reference);
    return status;
}
