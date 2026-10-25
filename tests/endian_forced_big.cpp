/*
 * Exercises the big-endian branch of port/endian.h on a little-endian host.
 *
 * This is not a big-endian memory simulation: the host still interprets the
 * memcpy results in its own order. It proves the branch is reached and wired
 * to Swap*, which the normal host test cannot do because the branch is
 * compiled out there. Byte expectations below are therefore relative to a
 * little-endian host and only checked when one is running the tests.
 */
#define OPENCMR2_TEST_FORCE_BIG_ENDIAN 1
#include "port/endian.h"

#include <string.h>

int EndianForcedBigSelfTest(void)
{
#if defined(OPENCMR2_LITTLE_ENDIAN)
    unsigned char bytes[8];
    const uint16_t values16[2] = {0x1234, 0xabcd};
    uint16_t read16[2] = {0, 0};

    WriteLE16(bytes, 0x1234);
    if (!(bytes[0] == 0x12 && bytes[1] == 0x34)) return 0;
    if (ReadLE16(bytes) != 0x1234) return 0;

    WriteLE32(bytes, 0x12345678u);
    if (!(bytes[0] == 0x12 && bytes[1] == 0x34 && bytes[2] == 0x56 && bytes[3] == 0x78))
        return 0;
    if (ReadLE32(bytes) != 0x12345678u) return 0;

    {
        /* Little-endian encoding of 0x12345678 read on a little-endian host. */
        static const unsigned char raw[] = {0x78, 0x56, 0x34, 0x12};
        if (ReadLE32(raw) != Swap32(0x12345678u)) return 0;
    }

    WriteLEFloat(bytes, 1.0f);
    if (!(bytes[0] == 0x3f && bytes[1] == 0x80 && bytes[2] == 0x00 && bytes[3] == 0x00))
        return 0;
    {
        const float value = ReadLEFloat(bytes);
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        if (bits != 0x3f800000u) return 0;
    }

    WriteLE16Array(bytes, values16, 2);
    if (!(bytes[0] == 0x12 && bytes[1] == 0x34 && bytes[2] == 0xab && bytes[3] == 0xcd))
        return 0;
    ReadLE16Array(read16, bytes, 2);
    if (memcmp(read16, values16, sizeof(values16)) != 0) return 0;
#endif

    return 1;
}
