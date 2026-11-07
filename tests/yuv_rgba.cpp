// YuvRgba must reproduce the decoder's YUV -> BGRX formula exactly (R, G, B
// order swapped to RGBA), honour macroblock masks, cropping and row runs.
#include "bink_fixture.h"
#include "video/yuv_rgba.h"

#include <string>

using namespace BinkTest;

namespace {
uint8_t Clamp8(int value) { return uint8_t(value < 0 ? 0 : value > 255 ? 255 : value); }
// The decoder's formula (bink_decoder.cpp, ConvertBgrx), written as RGBA.
void Reference(int y, int u, int v, uint8_t out[4]) {
    int luma = std::max(y - 16, 0);
    u -= 128; v -= 128;
    out[0] = Clamp8((298 * luma + 409 * v + 128) >> 8);
    out[1] = Clamp8((298 * luma - 100 * u - 208 * v + 128) >> 8);
    out[2] = Clamp8((298 * luma + 516 * u + 128) >> 8);
    out[3] = 255;
}

struct Frame420 {
    unsigned width, height, yStride, cStride, mbColumns, mbRows;
    std::vector<uint8_t> y, u, v, mask;
    Bink::Planes planes;
    Frame420(unsigned w, unsigned h) : width(w), height(h) {
        mbColumns = (w + 15) / 16; mbRows = (h + 15) / 16;
        yStride = mbColumns * 16; cStride = yStride / 2;
        y.assign(size_t(yStride) * mbRows * 16, 0);
        u.assign(size_t(cStride) * mbRows * 8, 0);
        v = u;
        mask.assign(size_t(mbColumns) * mbRows, 1);
    }
    void Fill(uint32_t &seed) {
        for (auto *plane : {&y, &u, &v})
            for (uint8_t &byte : *plane) { seed = seed * 1664525 + 1013904223; byte = uint8_t(seed >> 24); }
    }
    const Bink::Planes &Planes() {
        planes = {y.data(), u.data(), v.data(), yStride, cStride, mask.data(), mbColumns, mbRows};
        return planes;
    }
};

void CheckPixels(Frame420 &f, const std::vector<uint8_t> &rgba, size_t pitch, const uint8_t *skipMask,
                 const char *message) {
    for (unsigned y = 0; y < f.height; ++y)
        for (unsigned x = 0; x < f.width; ++x) {
            if (skipMask && !skipMask[(y / 16) * f.mbColumns + x / 16]) continue;
            uint8_t expected[4];
            Reference(f.y[y * f.yStride + x], f.u[(y / 2) * f.cStride + x / 2],
                      f.v[(y / 2) * f.cStride + x / 2], expected);
            Check(std::memcmp(&rgba[y * pitch + x * 4], expected, 4) == 0, message);
        }
}

void TestExhaustive() {
    // Every Y x U x V triple: y varies along x, (u, v) per call.
    Frame420 f(256, 2);
    for (unsigned x = 0; x < 256; ++x) f.y[x] = f.y[f.yStride + x] = x;
    YuvRgba::Tables tables;
    YuvRgba::InitTables(&tables);
    std::vector<uint8_t> rgba(256 * 2 * 4);
    YuvRgba::RowRun runs[1];
    for (unsigned u = 0; u < 256; ++u)
        for (unsigned v = 0; v < 256; ++v) {
            std::fill(f.u.begin(), f.u.end(), uint8_t(u));
            std::fill(f.v.begin(), f.v.end(), uint8_t(v));
            YuvRgba::Convert(tables, f.Planes(), nullptr, 256, 2, rgba.data(), 256 * 4, runs);
            for (unsigned i = 0; i < 512; ++i) {
                uint8_t expected[4];
                Reference(i % 256, u, v, expected);
                if (std::memcmp(&rgba[i * 4], expected, 4) != 0) {
                    std::fprintf(stderr, "y=%u u=%u v=%u\n", i % 256, u, v);
                    Check(false, "exhaustive YUV conversion");
                }
            }
        }
}

void TestRandomSizesAndMasks() {
    YuvRgba::Tables tables;
    YuvRgba::InitTables(&tables);
    uint32_t seed = 0xabcdef;
    const unsigned sizes[][2] = {{1, 1}, {2, 2}, {15, 17}, {16, 16}, {17, 15}, {33, 49}, {64, 32}, {47, 5}};
    for (auto &size : sizes) {
        Frame420 f(size[0], size[1]);
        f.Fill(seed);
        const size_t pitch = size_t(f.width) * 4 + 12; // not tightly packed
        std::vector<uint8_t> rgba(pitch * f.height);
        YuvRgba::RowRun runs[16];
        // All macroblocks (nullptr mask), then checkerboard masks over a sentinel.
        unsigned count = YuvRgba::Convert(tables, f.Planes(), nullptr, f.width, f.height, rgba.data(), pitch, runs);
        Check(count == 1 && runs[0].y == 0 && runs[0].rows == f.height, "full conversion runs");
        CheckPixels(f, rgba, pitch, nullptr, "random planes");
        for (unsigned pattern = 0; pattern < 3; ++pattern) {
            for (size_t i = 0; i < f.mask.size(); ++i) f.mask[i] = pattern == 0 ? (i & 1) : pattern == 1 ? !(i & 1) : 0;
            f.Fill(seed);
            std::vector<uint8_t> before(rgba.size(), 0x5A), out = before;
            count = YuvRgba::Convert(tables, f.Planes(), f.mask.data(), f.width, f.height, out.data(), pitch, runs);
            // Unflagged macroblocks keep the sentinel; flagged ones are converted.
            for (unsigned y = 0; y < f.height; ++y)
                for (unsigned x = 0; x < f.width; ++x) {
                    bool flagged = f.mask[(y / 16) * f.mbColumns + x / 16];
                    uint8_t expected[4];
                    Reference(f.y[y * f.yStride + x], f.u[(y / 2) * f.cStride + x / 2],
                              f.v[(y / 2) * f.cStride + x / 2], expected);
                    const uint8_t *got = &out[y * pitch + x * 4];
                    Check(flagged ? std::memcmp(got, expected, 4) == 0
                                  : std::memcmp(got, &before[y * pitch + x * 4], 4) == 0, "masked conversion");
                }
            // Padding between rows is never written.
            for (unsigned y = 0; y < f.height; ++y)
                for (size_t b = f.width * 4; b < pitch; ++b) Check(out[y * pitch + b] == 0x5A, "pitch padding written");
            // Runs cover exactly the pixel rows of macroblock rows with a flagged macroblock, merged.
            std::vector<bool> expectedRows(f.height, false);
            for (unsigned mby = 0; mby < f.mbRows; ++mby)
                for (unsigned mbx = 0; mbx < f.mbColumns; ++mbx)
                    if (f.mask[mby * f.mbColumns + mbx])
                        for (unsigned y = mby * 16; y < std::min(f.height, mby * 16 + 16); ++y) expectedRows[y] = true;
            std::vector<bool> gotRows(f.height, false);
            for (unsigned r = 0; r < count; ++r) {
                Check(r == 0 || runs[r].y > runs[r - 1].y + runs[r - 1].rows, "runs must be separate and ordered");
                for (unsigned y = runs[r].y; y < runs[r].y + runs[r].rows; ++y) { Check(y < f.height, "run past end"); gotRows[y] = true; }
            }
            Check(expectedRows == gotRows, "row runs");
        }
    }
}

// The converter on a real decoded stream equals the decoder's BGRX output.
void TestAgainstDecoder(const std::vector<uint8_t> &data, unsigned frames, unsigned width, unsigned height) {
    Bink::Decoder planesDecoder, bgrxDecoder;
    std::vector<uint8_t> a = data, b = data;
    Check(Open(planesDecoder, a, -1) && Open(bgrxDecoder, b, -1), "open stream for conversion");
    YuvRgba::Tables tables;
    YuvRgba::InitTables(&tables);
    std::vector<uint8_t> rgba(size_t(width) * height * 4, 0);
    std::vector<YuvRgba::RowRun> runs(planesDecoder.GetInfo().height / 16 + 2);
    Bink::Frame fa, fb;
    Bink::Planes planes = {};
    for (unsigned i = 0; i < frames; ++i) {
        Check(planesDecoder.Decode(fa, &planes) && bgrxDecoder.Decode(fb), "decode for conversion");
        YuvRgba::Convert(tables, planes, planes.changed, width, height, rgba.data(), size_t(width) * 4, runs.data());
        for (size_t p = 0; p < size_t(width) * height; ++p)
            Check(rgba[p * 4] == fb.bgrx[p * 4 + 2] && rgba[p * 4 + 1] == fb.bgrx[p * 4 + 1] &&
                  rgba[p * 4 + 2] == fb.bgrx[p * 4] && rgba[p * 4 + 3] == fb.bgrx[p * 4 + 3],
                  "converter differs from decoder BGRX");
    }
}
}

int main() {
    TestExhaustive();
    TestRandomSizesAndMasks();
    auto synthetic = Container(9, 9, {Video(9, 9, 6, 81), Video(9, 9, 0, 0), Video(9, 9, 2, 0)});
    TestAgainstDecoder(synthetic, 3, 9, 9);
    if (const char *dir = std::getenv("OPENCMR2_BINK_TEST_DATA_DIR")) {
        std::string path = std::string(dir) + "/FrontEnd/Videos/cm.bik";
        if (FILE *file = std::fopen(path.c_str(), "rb")) {
            std::fseek(file, 0, SEEK_END);
            std::vector<uint8_t> movie(size_t(std::ftell(file)));
            std::rewind(file);
            Check(std::fread(movie.data(), 1, movie.size(), file) == movie.size(), "read cm.bik");
            std::fclose(file);
            TestAgainstDecoder(movie, 120, 640, 480);
        }
    }
    std::puts("YUV to RGBA conversion tests passed");
}
