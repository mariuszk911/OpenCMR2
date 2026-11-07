// Standalone Bink 1 decoder. No platform, graphics or audio dependencies.
#ifndef OPENCMR2_BINK_DECODER_H
#define OPENCMR2_BINK_DECODER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Bink {

struct Reader {
    void *context;
    uint32_t size;
    bool (*readAt)(void *context, uint32_t offset, void *dst, uint32_t bytes);
};

struct Info {
    uint32_t width = 0, height = 0, frames = 0;
    uint32_t fpsNumerator = 0, fpsDenominator = 0;
    uint32_t sampleRate = 0;
    unsigned channels = 0;
};

struct Frame {
    std::vector<uint8_t> bgrx;
    // Interleaved float PCM for the selected track, at Info::sampleRate.
    std::vector<float> audio;
};

// Decoded planes of the latest frame and which 16x16 macroblocks changed.
// Pointers stay valid until the next Decode or Open. The alpha plane is not
// tracked: the movies have none and playback is opaque.
struct Planes {
    const uint8_t *y, *u, *v;
    uint32_t yStride, cStride;   // padded strides (multiples of 16)
    // One byte per macroblock, row-major, nonzero when any of its four luma 8x8
    // blocks or its U or V 8x8 block is not a skip block. Conservative: pixels
    // of an unflagged macroblock are identical to the previous frame's. The
    // first frame after Open flags every macroblock.
    const uint8_t *changed;
    uint32_t mbColumns, mbRows;  // (width + 15) / 16, (height + 15) / 16
};

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    // Reader remains owned by caller. Track is a Bink track ID (language);
    // unavailable IDs fall back to the first track, -1 selects silence.
    bool Open(Reader reader, int track);
    // Sequential decode; on corruption decoding stops until Open is called.
    bool Decode(Frame &frame);
    // With planes != nullptr the picture is not converted: frame.bgrx is left
    // empty and *planes describes the decoded YUV planes instead. On failure
    // *planes is zeroed.
    bool Decode(Frame &frame, Planes *planes);
    const Info &GetInfo() const;
    const std::string &Error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Bink
#endif
