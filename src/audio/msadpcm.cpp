// Microsoft ADPCM decoder: see port/audio.h. The output is 16-bit little-endian
// PCM on every target, the same layout as the samples loaded from .wav files.

#include "port/audio.h"

namespace {

inline void StoreSample(BYTE *dst, DWORD index, int sample)
{
    WriteLE16(dst + index * 2, (uint16_t)(int16_t)sample);
}

} // namespace

extern "C" DWORD Audio_DecodeMsAdpcm(const AudioWaveFormat *format, const BYTE *src, DWORD srcBytes, BYTE *dst,
                                     DWORD dstBytes)
{
    static const int adaptation[16] = { 230, 230, 230, 230, 307, 409, 512, 614,
                                        768, 614, 512, 409, 307, 230, 230, 230 };
    static const short standardCoefs[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 },
                                               { 240, 0 }, { 460, -208 }, { 392, -232 } };
    short coefs[32][2];
    int coefCount = 7;
    int channels = format->nChannels;
    DWORD blockAlign = format->nBlockAlign;
    DWORD written = 0;

    memcpy(coefs, standardCoefs, sizeof(standardCoefs));
    if (format->cbSize >= 4) {
        const BYTE *extra = (const BYTE *)format + sizeof(AudioWaveFormat);
        WORD count = (WORD)(extra[2] | extra[3] << 8);
        if (count >= 7 && count <= 32 && format->cbSize >= 4 + count * 4) {
            coefCount = count;
            for (int i = 0; i < count; i++) {
                coefs[i][0] = (short)(extra[4 + i * 4] | extra[5 + i * 4] << 8);
                coefs[i][1] = (short)(extra[6 + i * 4] | extra[7 + i * 4] << 8);
            }
        }
    }
    if (channels < 1 || channels > 2 || blockAlign < (DWORD)(7 * channels))
        return 0;

    DWORD outMax = dstBytes / 2;
    for (DWORD offset = 0; offset + blockAlign <= srcBytes; offset += blockAlign) {
        const BYTE *p = src + offset;
        int predictor[2], delta[2], s1[2], s2[2];
        for (int c = 0; c < channels; c++)
            predictor[c] = p[c] < coefCount ? p[c] : 0;
        p += channels;
        for (int c = 0; c < channels; c++, p += 2)
            delta[c] = (int16_t)(uint16_t)(p[0] | p[1] << 8);
        for (int c = 0; c < channels; c++, p += 2)
            s1[c] = (int16_t)(uint16_t)(p[0] | p[1] << 8);
        for (int c = 0; c < channels; c++, p += 2)
            s2[c] = (int16_t)(uint16_t)(p[0] | p[1] << 8);
        // The header's two samples come out oldest first.
        for (int c = 0; c < channels && written < outMax; c++)
            StoreSample(dst, written++, s2[c]);
        for (int c = 0; c < channels && written < outMax; c++)
            StoreSample(dst, written++, s1[c]);
        const BYTE *end = src + offset + blockAlign;
        int channel = 0;
        for (; p < end && written < outMax; p++) {
            for (int half = 0; half < 2 && written < outMax; half++) {
                int nibble = half == 0 ? p[0] >> 4 : p[0] & 0xf;
                int signedNibble = nibble >= 8 ? nibble - 16 : nibble;
                int c = channel;
                int prediction = (s1[c] * coefs[predictor[c]][0] + s2[c] * coefs[predictor[c]][1]) / 256;
                int sample = prediction + signedNibble * delta[c];
                sample = sample > 32767 ? 32767 : (sample < -32768 ? -32768 : sample);
                StoreSample(dst, written++, sample);
                s2[c] = s1[c];
                s1[c] = sample;
                delta[c] = delta[c] * adaptation[nibble] / 256;
                if (delta[c] < 16)
                    delta[c] = 16;
                channel = (channel + 1) % channels;
            }
        }
    }
    return written * 2;
}
