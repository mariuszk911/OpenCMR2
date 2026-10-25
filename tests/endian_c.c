/*
 * Proves that port/endian.h is usable from C, which the zlib and libfix
 * translation units need.
 */
#include "port/endian.h"

int EndianC_SelfTest(void)
{
    unsigned char bytes[8];
    int ok = 1;

    WriteLE16(bytes, 0x1234);
    ok = ok && bytes[0] == 0x34 && bytes[1] == 0x12;
    ok = ok && ReadLE16(bytes) == 0x1234;

    WriteLE32(bytes, 0x89abcdefu);
    ok = ok && bytes[0] == 0xef && bytes[3] == 0x89;
    ok = ok && ReadLE32(bytes) == 0x89abcdefu;

    ok = ok && Swap64(0x0123456789abcdefULL) == 0xefcdab8967452301ULL;

    {
        const float value = 1.25f;
        WriteLEFloat(bytes, value);
        ok = ok && ReadLEFloat(bytes) == value;
    }

    {
        const uint16_t host[2] = {0x1234, 0xabcd};
        uint16_t read[2] = {0, 0};

        WriteLE16Array(bytes, host, 2);
        ok = ok && bytes[0] == 0x34 && bytes[1] == 0x12 && bytes[2] == 0xcd && bytes[3] == 0xab;
        ReadLE16Array(read, bytes, 2);
        ok = ok && read[0] == host[0] && read[1] == host[1];
    }

    return ok;
}
