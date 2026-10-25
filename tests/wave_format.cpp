#include "port/audio.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    {
        static const unsigned char raw[16] = {
            0x01, 0x00, 0x02, 0x00,
            0x44, 0xac, 0x00, 0x00,
            0x88, 0x58, 0x01, 0x00,
            0x04, 0x00, 0x10, 0x00
        };
        AudioWaveFormat format;
        memset(&format, 0, sizeof(format));
        memcpy(&format, raw, sizeof(raw));
        AudioWaveFormatFromLE(&format);
        REQUIRE(format.wFormatTag == 1);
        REQUIRE(format.nChannels == 2);
        REQUIRE(format.nSamplesPerSec == 44100);
        REQUIRE(format.nAvgBytesPerSec == 88200);
        REQUIRE(format.nBlockAlign == 4);
        REQUIRE(format.wBitsPerSample == 16);
        REQUIRE(format.cbSize == 0);
    }

    {
        static const unsigned char raw[18] = {
            0x02, 0x00, 0x01, 0x00,
            0x11, 0x2b, 0x00, 0x00,
            0x22, 0x56, 0x00, 0x00,
            0x00, 0x01, 0x04, 0x00,
            0x20, 0x00
        };
        AudioWaveFormat format;
        memset(&format, 0, sizeof(format));
        memcpy(&format, raw, sizeof(raw));
        AudioWaveFormatFromLE(&format);
        REQUIRE(format.wFormatTag == 2);
        REQUIRE(format.nChannels == 1);
        REQUIRE(format.nSamplesPerSec == 11025);
        REQUIRE(format.nAvgBytesPerSec == 22050);
        REQUIRE(format.nBlockAlign == 256);
        REQUIRE(format.wBitsPerSample == 4);
        REQUIRE(format.cbSize == 32);
    }

    return 0;
}
