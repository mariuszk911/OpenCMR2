#include "bink_fixture.h"

#include <string>

using namespace BinkTest;

namespace {
// One plane with explicit block types, in decode order (a scaled block at
// (even, even) consumes one entry; its right neighbour none; the row below
// consumes one entry for the pair). `sub` holds the sub-block types of scaled
// blocks and `fills` the number of colours the fill blocks read.
struct PlaneSpec { std::vector<uint8_t> types, sub; unsigned fills; };
void SpecPlane(Writer &bits, unsigned width, const PlaneSpec &spec) {
    for (int source = 0; source < 9; ++source) {
        if (source == 2) for (int i = 0; i < 16; ++i) bits.Write(0, 4);
        if (source != 6 && source != 7) bits.Write(0, 4);
    }
    for (int source = 0; source < 9; ++source) {
        // The decoder reads a new count whenever a bundle is fully consumed at the
        // start of a block row, so the bundles used only in early rows carry one
        // unused spare entry.
        unsigned used = source == 0 ? spec.types.size() : source == 1 ? spec.sub.size()
                      : source == 2 ? spec.fills : 0;
        unsigned count = used && source ? used + 1 : used;
        bits.Write(count, Length(width, source));
        if (!count) continue;
        if (source == 2) { bits.Write(1, 1); bits.Write(8, 4); bits.Write(0, 4); continue; }
        bits.Write(0, 1); // explicit symbols through the identity tree
        for (unsigned i = 0; i < count; ++i)
            bits.Write(i < used ? (source ? spec.sub : spec.types)[i] : 0, 4);
    }
    bits.Align();
}
std::vector<uint8_t> SpecVideo(unsigned width, const PlaneSpec &luma, const PlaneSpec &v, const PlaneSpec &u) {
    Writer bits;
    bits.Write(0, 32);
    SpecPlane(bits, width, luma);
    SpecPlane(bits, width / 2, v);
    SpecPlane(bits, width / 2, u);
    return bits.data;
}
std::vector<uint8_t> Repeat(unsigned count, uint8_t value = 0) { return std::vector<uint8_t>(count, value); }

uint8_t Clamp8(int value) { return uint8_t(value < 0 ? 0 : value > 255 ? 255 : value); }
// The decoder's own YUV -> BGRX formula, for the macroblocks of one frame.
void ConvertMacroblock(const Bink::Planes &planes, unsigned width, unsigned height,
                       unsigned mbx, unsigned mby, std::vector<uint8_t> &bgrx) {
    for (unsigned y = mby * 16; y < std::min(height, mby * 16 + 16); ++y)
        for (unsigned x = mbx * 16; x < std::min(width, mbx * 16 + 16); ++x) {
            int luma = std::max(int(planes.y[y * planes.yStride + x]) - 16, 0);
            int u = int(planes.u[(y / 2) * planes.cStride + x / 2]) - 128;
            int v = int(planes.v[(y / 2) * planes.cStride + x / 2]) - 128;
            uint8_t *dst = &bgrx[(size_t(y) * width + x) * 4];
            dst[0] = Clamp8((298 * luma + 516 * u + 128) >> 8);
            dst[1] = Clamp8((298 * luma - 100 * u - 208 * v + 128) >> 8);
            dst[2] = Clamp8((298 * luma + 409 * v + 128) >> 8);
            dst[3] = 255;
        }
}

void TestChangedMap() {
    const PlaneSpec lumaSkip = {Repeat(16), {}, 0}, chromaSkip = {Repeat(4), {}, 0};
    auto video = [&](PlaneSpec luma, PlaneSpec v, PlaneSpec u) { return SpecVideo(32, luma, v, u); };
    PlaneSpec lumaFill = lumaSkip; lumaFill.types[2] = 6; lumaFill.fills = 1;          // block (2,0): MB 1
    PlaneSpec lumaFill2 = lumaSkip; lumaFill2.types[13] = 6; lumaFill2.fills = 1;      // block (1,3): MB 2
    PlaneSpec chromaFill = chromaSkip; chromaFill.types[3] = 6; chromaFill.fills = 1;  // block (1,1): MB 3
    PlaneSpec lumaScaled = {{0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {6}, 1};      // scaled at (2,0): MB 1
    PlaneSpec chromaScaled = {{1, 1}, {6}, 1};                                         // scaled: all four MBs
    auto data = Container(32, 32, {
        video(lumaSkip, chromaSkip, chromaSkip), video(lumaSkip, chromaSkip, chromaSkip),
        video(lumaFill, chromaSkip, chromaSkip), video(lumaFill2, chromaSkip, chromaSkip),
        video(lumaSkip, chromaFill, chromaSkip), video(lumaScaled, chromaSkip, chromaSkip),
        video(lumaSkip, chromaSkip, chromaScaled)});
    const std::vector<std::vector<uint8_t>> expected = {
        {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 1, 0, 0}, {1, 1, 1, 1}};
    Bink::Decoder decoder;
    Bink::Frame frame;
    Bink::Planes planes = {};
    Check(Open(decoder, data), "cannot open macroblock map movie");
    for (size_t i = 0; i < expected.size(); ++i) {
        const bool decoded = decoder.Decode(frame, &planes);
        Check(decoded, decoder.Error().c_str());
        Check(frame.bgrx.empty(), "plane decode must not convert");
        Check(planes.mbColumns == 2 && planes.mbRows == 2 && planes.yStride == 32 && planes.cStride == 16,
              "plane geometry");
        Check(planes.y && planes.u && planes.v && planes.changed, "plane pointers");
        Check(std::vector<uint8_t>(planes.changed, planes.changed + 4) == expected[i], "changed macroblock map");
    }
    Check(!decoder.Decode(frame, &planes) && !planes.y && !planes.changed, "planes must be cleared on failure");
}

// For every frame of a real movie, converting only the flagged macroblocks into
// a persistent buffer must equal the full conversion: the map is conservative.
void TestChangedMapAgainstMovie(const char *path) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return;
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    auto read = [](void *context, uint32_t offset, void *dst, uint32_t bytes) {
        FILE *f = static_cast<FILE *>(context);
        return std::fseek(f, offset, SEEK_SET) == 0 && std::fread(dst, 1, bytes, f) == bytes;
    };
    Bink::Decoder mapped, full;
    Check(mapped.Open({file, uint32_t(size), read}, -1) && full.Open({file, uint32_t(size), read}, -1),
          "cannot open movie for the macroblock map check");
    const Bink::Info info = mapped.GetInfo();
    std::vector<uint8_t> persistent(size_t(info.width) * info.height * 4, 0);
    Bink::Frame a, b;
    Bink::Planes planes = {};
    size_t flagged = 0, total = 0;
    for (unsigned i = 0; i < info.frames; ++i) {
        Check(mapped.Decode(a, &planes) && full.Decode(b), "movie decode");
        for (unsigned y = 0; y < planes.mbRows; ++y)
            for (unsigned x = 0; x < planes.mbColumns; ++x) {
                ++total;
                if (!planes.changed[y * planes.mbColumns + x]) continue;
                ++flagged;
                ConvertMacroblock(planes, info.width, info.height, x, y, persistent);
            }
        Check(persistent == b.bgrx, "changed-macroblock conversion differs from full conversion");
    }
    Check(flagged < total, "movie never skips a macroblock");
    std::fclose(file);
}
}

int main() {
    Bink::Decoder decoder;
    Bink::Frame frame;
    auto data = Container(9, 9, {Video(9, 9, 6, 81), Video(9, 9, 0, 0), Video(9, 9, 2, 0)});
    Check(Open(decoder, data), "cannot open synthetic movie");
    Check(decoder.GetInfo().fpsNumerator == 30000 && decoder.GetInfo().fpsDenominator == 1001, "fractional fps");
    std::vector<uint8_t> previous;
    for (int i = 0; i < 3; ++i) {
        Check(decoder.Decode(frame), decoder.Error().c_str());
        Check(frame.bgrx.size() == 9 * 9 * 4, "cropping padded planes");
        for (size_t pixel = 0; pixel < 81; ++pixel) {
            Check(frame.bgrx[pixel * 4] == 76 && frame.bgrx[pixel * 4 + 1] == 76 &&
                  frame.bgrx[pixel * 4 + 2] == 76 && frame.bgrx[pixel * 4 + 3] == 255, "fill/skip/motion pixels");
        }
        if (i) Check(previous == frame.bgrx, "delta frame reference");
        previous = frame.bgrx;
    }
    Check(!decoder.Decode(frame), "must stop after final frame");
    for (bool dct : {false, true}) {
        auto audio = Audio(dct), video = Video(8, 8, 6, 16);
        std::vector<uint8_t> packet(4); Put32(packet, 0, audio.size());
        packet.insert(packet.end(), audio.begin(), audio.end()); packet.insert(packet.end(), video.begin(), video.end());
        auto sound = Container(8, 8, {packet, packet}, 0x2000 | (dct ? 0x1000 : 0));
        Check(Open(decoder, sound, 7), "open audio track by ID");
        Check(decoder.GetInfo().sampleRate == 8000 && decoder.GetInfo().channels == 2, "audio format");
        for (int i = 0; i < 2; ++i) {
            Check(decoder.Decode(frame), decoder.Error().c_str());
            Check(frame.audio.size() == 960, "stereo audio sample count");
            auto sampleAt = [&](unsigned index, unsigned channel) {
                constexpr double pi = 3.14159265358979323846;
                if (!dct) return float((index % 2 ? 0 : 2.0) / std::sqrt(1024.0) / 32768);
                double sign = channel ? -1 : 1;
                return float(2 * (1 + sign * std::cos(pi * (index + 0.5) / 512)) / std::sqrt(512.0) / 32768);
            };
            for (unsigned sample = 0; sample < frame.audio.size(); ++sample) {
                unsigned index = dct ? sample / 2 : sample, channel = dct ? sample % 2 : 0;
                float expected = sampleAt(index, channel);
                unsigned overlap = dct ? 32 : 64;
                if (i && index < overlap) {
                    unsigned weight = dct ? index * 2 + channel : index;
                    float previous = sampleAt((dct ? 512 : 1024) - overlap + index, channel);
                    expected = (previous * (64 - weight) + expected * weight) / 64;
                }
                Check(std::abs(frame.audio[sample] - expected) < 1e-9, "audio transform/interleave/overlap");
            }
        }
        Check(Open(decoder, sound, 123) && decoder.Decode(frame) && frame.audio.size() == 960, "track fallback");
        Check(Open(decoder, sound, -1) && decoder.Decode(frame) && frame.audio.empty(), "silent track");
        auto broken = sound; Put32(broken, 64, 0xffffffff); // first packet audio length
        Check(Open(decoder, broken) && !decoder.Decode(frame), "audio length bounds");
    }
    // Every truncation must fail to open or decode, never read outside the input.
    for (size_t size = 0; size < data.size(); ++size) {
        auto truncated = std::vector<uint8_t>(data.begin(), data.begin() + size);
        Check(!Open(decoder, truncated), "truncated movie accepted");
    }
    for (auto field : {4, 8, 12, 20, 24, 28, 32, 40, 44}) {
        auto corrupt = data; Put32(corrupt, field, 0xffffffff);
        Check(!Open(decoder, corrupt), "invalid header/index accepted");
    }
    auto invalidType = Container(8, 8, {Video(8, 8, 15, 0)});
    Check(Open(decoder, invalidType) && !decoder.Decode(frame), "invalid block type accepted");
    Check(frame.audio.empty() && frame.bgrx.empty(), "partial frame exposed after corruption");
    Check(!decoder.Decode(frame), "corrupt decoder resumed");
    // Deterministic mutations cover trees, bundles, bit exhaustion and coefficient lists.
    uint32_t random = 0x51f15e;
    for (unsigned i = 0; i < 2000; ++i) {
        auto mutated = data;
        random = random * 1664525 + 1013904223;
        size_t offset = 56 + random % (mutated.size() - 56);
        random = random * 1664525 + 1013904223;
        mutated[offset] ^= 1u << (random & 7);
        if (Open(decoder, mutated)) while (decoder.Decode(frame)) {}
    }
    TestChangedMap();
    if (const char *dir = std::getenv("OPENCMR2_BINK_TEST_DATA_DIR"))
        TestChangedMapAgainstMovie((std::string(dir) + "/FrontEnd/Videos/cm.bik").c_str());
    std::puts("Bink container, video, audio, truncation and corruption tests passed");
}
