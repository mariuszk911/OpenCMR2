#include "video/yuv_rgba.h"

namespace YuvRgba {

void InitTables(Tables *tables) {
    for (int i = 0; i < 256; ++i) {
        int luma = i - 16 > 0 ? i - 16 : 0;
        tables->luma[i] = 298 * luma + 128 + (Tables::kBias << 8);
        tables->bu[i] = 516 * (i - 128);
        tables->gu[i] = -100 * (i - 128);
        tables->gv[i] = -208 * (i - 128);
        tables->rv[i] = 409 * (i - 128);
    }
    for (int i = 0; i < Tables::kClampSize; ++i) {
        int value = i - Tables::kBias;
        tables->clamp[i] = uint8_t(value < 0 ? 0 : value > 255 ? 255 : value);
    }
}

namespace {
// The chroma terms shared by a 2x2 pixel group.
struct Chroma { int32_t r, g, b; };

inline void Pixel(const Tables &t, uint8_t *dst, int32_t luma, const Chroma &c) {
    dst[0] = t.clamp[(luma + c.r) >> 8];
    dst[1] = t.clamp[(luma + c.g) >> 8];
    dst[2] = t.clamp[(luma + c.b) >> 8];
    dst[3] = 0xFF;
}
} // namespace

unsigned Convert(const Tables &t, const Bink::Planes &planes, const uint8_t *mask,
                 uint32_t width, uint32_t height, uint8_t *rgba, size_t pitch, RowRun *runs) {
    unsigned runCount = 0;
    for (uint32_t mby = 0; mby < planes.mbRows; ++mby) {
        const uint32_t y0 = mby * 16, y1 = y0 + 16 < height ? y0 + 16 : height;
        bool rowTouched = false;
        for (uint32_t mbx = 0; mbx < planes.mbColumns; ++mbx) {
            if (mask && !mask[size_t(mby) * planes.mbColumns + mbx]) continue;
            rowTouched = true;
            const uint32_t x0 = mbx * 16, x1 = x0 + 16 < width ? x0 + 16 : width;
            // Macroblocks start on even coordinates, so 2x2 groups never straddle one.
            for (uint32_t y = y0; y < y1; y += 2) {
                const bool second = y + 1 < y1;
                const uint8_t *ly = planes.y + size_t(y) * planes.yStride;
                const uint8_t *cu = planes.u + size_t(y / 2) * planes.cStride;
                const uint8_t *cv = planes.v + size_t(y / 2) * planes.cStride;
                uint8_t *d0 = rgba + size_t(y) * pitch;
                uint8_t *d1 = d0 + pitch;
                for (uint32_t x = x0; x < x1; x += 2) {
                    const uint32_t cx = x / 2;
                    const Chroma c = {t.rv[cv[cx]], t.gu[cu[cx]] + t.gv[cv[cx]], t.bu[cu[cx]]};
                    const bool right = x + 1 < x1;
                    Pixel(t, d0 + x * 4, t.luma[ly[x]], c);
                    if (right) Pixel(t, d0 + (x + 1) * 4, t.luma[ly[x + 1]], c);
                    if (second) {
                        Pixel(t, d1 + x * 4, t.luma[ly[planes.yStride + x]], c);
                        if (right) Pixel(t, d1 + (x + 1) * 4, t.luma[ly[planes.yStride + x + 1]], c);
                    }
                }
            }
        }
        if (!rowTouched) continue;
        if (runCount && runs[runCount - 1].y + runs[runCount - 1].rows == y0)
            runs[runCount - 1].rows += y1 - y0;
        else {
            runs[runCount].y = y0;
            runs[runCount].rows = y1 - y0;
            ++runCount;
        }
    }
    return runCount;
}

} // namespace YuvRgba
