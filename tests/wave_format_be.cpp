/* The forced branch is a host test seam: it proves the WAV normalizer applies
 * the byte swap when the target is big-endian. */
#define OPENCMR2_TEST_FORCE_BIG_ENDIAN 1
#include "port/audio.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    static const unsigned char raw[18] = {
        0x01, 0x00, 0x02, 0x00,
        0x44, 0xac, 0x00, 0x00,
        0x88, 0x58, 0x01, 0x00,
        0x04, 0x00, 0x10, 0x00,
        0x20, 0x00
    };
    AudioWaveFormat format;
    memset(&format, 0, sizeof(format));
    memcpy(&format, raw, sizeof(raw));
    AudioWaveFormatFromLE(&format);
    REQUIRE(format.wFormatTag == Swap16(1));
    REQUIRE(format.nChannels == Swap16(2));
    REQUIRE(format.nSamplesPerSec == Swap32(44100));
    REQUIRE(format.nAvgBytesPerSec == Swap32(88200));
    REQUIRE(format.nBlockAlign == Swap16(4));
    REQUIRE(format.wBitsPerSample == Swap16(16));
    REQUIRE(format.cbSize == Swap16(32));
    return 0;
}
