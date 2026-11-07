// Table-driven YUV 4:2:0 -> RGBA8 conversion for decoded Bink planes.
// Bit-identical to the decoder's own YUV -> BGRX formula, only the byte order
// differs: bytes are written R, G, B, 0xFF. No platform dependencies.
#ifndef OPENCMR2_YUV_RGBA_H
#define OPENCMR2_YUV_RGBA_H

#include "video/bink_decoder.h"

#include <stddef.h>
#include <stdint.h>

namespace YuvRgba {

// Lookup tables, built once into caller-owned storage (no static state).
// ~6.7 KB. The sum of the per-channel terms carries a bias of kBias << 8 so the
// shifted index is never negative and no implementation-defined right shift of
// a negative value is needed. The largest index is (298 * 239 + 516 * 127 + 128
// + (kBias << 8)) >> 8 = 1558, the smallest is above 800.
struct Tables {
    enum { kBias = 1024, kClampSize = 1600 };
    int32_t luma[256];    // 298 * max(y - 16, 0) + 128 + (kBias << 8)
    int32_t bu[256];      // 516 * (u - 128)
    int32_t gu[256];      // -100 * (u - 128)
    int32_t gv[256];      // -208 * (v - 128)
    int32_t rv[256];      // 409 * (v - 128)
    uint8_t clamp[kClampSize]; // clamp(index - kBias, 0, 255)
};

void InitTables(Tables *tables);

// A run of consecutive pixel rows touched by a conversion.
struct RowRun {
    uint32_t y, rows;
};

// Converts the macroblocks flagged in `mask` (one byte per macroblock,
// planes.mbColumns * planes.mbRows, row-major; nullptr converts all) from
// `planes` into `rgba` (pitch bytes per row), cropped to width x height.
// Macroblocks that are not flagged are not touched. `runs` receives the pixel
// row runs that contain a converted macroblock, merged where adjacent, and
// needs room for planes.mbRows entries. Returns the number of runs.
unsigned Convert(const Tables &tables, const Bink::Planes &planes, const uint8_t *mask,
                 uint32_t width, uint32_t height, uint8_t *rgba, size_t pitch, RowRun *runs);

} // namespace YuvRgba
#endif
