/*
 * Correctness check for the Bink planes path on the 68k target, without timing
 * or Amiga system calls (it needs the swapped stack linked in by amiga.cmake):
 *
 *     amiga_bink_planes_check FILE [FRAMES]
 *
 * Decodes with Decode(frame, &planes), converts the flagged macroblocks into a
 * persistent RGBA buffer with YuvRgba::Convert and prints the BGRX FNV64 over
 * all converted frames, which must equal the host's (docs/VIDEO.md; for a
 * frame limit use `bink_decode FILE 0 "" "" FRAMES`). It also compares every
 * frame with the decoder's own BGRX output and stops at the first difference.
 */
#include "video/bink_decoder.h"
#include "video/yuv_rgba.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace {
bool ReadAt(void *context, uint32_t offset, void *dst, uint32_t bytes)
{
    FILE *file = static_cast<FILE *>(context);
    return fseek(file, long(offset), SEEK_SET) == 0 && fread(dst, 1, bytes, file) == bytes;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: amiga_bink_planes_check FILE [FRAMES]\n");
        return 5;
    }
    FILE *a = fopen(argv[1], "rb");
    FILE *b = fopen(argv[1], "rb");
    if (!a || !b) {
        printf("planes_check: FAIL cannot open %s\n", argv[1]);
        if (a) fclose(a);
        if (b) fclose(b);
        return 10;
    }
    int status = 10;
    long size = fseek(a, 0, SEEK_END) == 0 ? ftell(a) : -1;
    Bink::Decoder planesDecoder, bgrxDecoder;
    YuvRgba::Tables *tables = new YuvRgba::Tables;
    if (size > 0 && size <= 0x7fffffff && planesDecoder.Open({a, uint32_t(size), ReadAt}, -1) &&
        bgrxDecoder.Open({b, uint32_t(size), ReadAt}, -1)) {
        const Bink::Info info = planesDecoder.GetInfo();
        const unsigned limit = argc > 2 ? (unsigned)atoi(argv[2]) : 0;
        const unsigned count = limit && limit < info.frames ? limit : info.frames;
        YuvRgba::InitTables(tables);
        std::vector<uint8_t> rgba(size_t(info.width) * info.height * 4, 0);
        std::vector<YuvRgba::RowRun> runs((info.height + 15) / 16);
        Bink::Frame planesFrame, bgrxFrame;
        Bink::Planes planes = {};
        uint64_t hash = 14695981039346656037ull;
        unsigned long changed = 0, total = 0;
        status = 0;
        for (unsigned i = 0; i < count && status == 0; ++i) {
            if (!planesDecoder.Decode(planesFrame, &planes) || !bgrxDecoder.Decode(bgrxFrame)) {
                printf("planes_check: FAIL frame %u: %s\n", i + 1, planesDecoder.Error().c_str());
                status = 10;
                break;
            }
            YuvRgba::Convert(*tables, planes, planes.changed, info.width, info.height, rgba.data(),
                             size_t(info.width) * 4, runs.data());
            for (unsigned m = 0; m < planes.mbColumns * planes.mbRows; ++m) {
                changed += planes.changed[m] != 0;
                ++total;
            }
            for (size_t p = 0; p < rgba.size(); p += 4) {
                const uint8_t bgrx[4] = {rgba[p + 2], rgba[p + 1], rgba[p], rgba[p + 3]};
                for (int k = 0; k < 4; ++k) {
                    if (bgrx[k] != bgrxFrame.bgrx[p + k]) {
                        printf("planes_check: FAIL frame %u pixel %lu byte %d: %02x expected %02x\n", i + 1,
                               (unsigned long)(p / 4), k, bgrx[k], bgrxFrame.bgrx[p + k]);
                        status = 10;
                        break;
                    }
                    hash ^= bgrx[k];
                    hash *= 1099511628211ull;
                }
                if (status) break;
            }
            if ((i + 1) % 25 == 0) {
                printf("planes_check: %u/%u frames ok\n", i + 1, count);
                fflush(stdout);
            }
        }
        if (status == 0)
            printf("planes_check: OK %u frames, changed MB %lu%%, BGRX FNV64 %08lx%08lx\n", count,
                   total ? changed * 100 / total : 0ul, (unsigned long)(hash >> 32),
                   (unsigned long)(hash & 0xffffffffu));
    } else {
        printf("planes_check: FAIL open: %s\n", planesDecoder.Error().c_str());
    }
    delete tables;
    fclose(a);
    fclose(b);
    return status;
}
