#include "port/endian.h"
#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

extern "C" int EndianC_SelfTest(void);
extern int EndianForcedBigSelfTest(void);

static uint32_t FloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float FloatFromBits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* The stored bytes are little-endian no matter what the host is. */
static int TestStoredByteOrder()
{
    unsigned char bytes[16];

    memset(bytes, 0xaa, sizeof(bytes));
    WriteLE16(bytes + 1, 0x1234);
    REQUIRE(bytes[0] == 0xaa && bytes[1] == 0x34 && bytes[2] == 0x12 && bytes[3] == 0xaa);

    memset(bytes, 0xaa, sizeof(bytes));
    WriteLE32(bytes + 3, 0x12345678u);
    REQUIRE(bytes[2] == 0xaa && bytes[3] == 0x78 && bytes[4] == 0x56 &&
            bytes[5] == 0x34 && bytes[6] == 0x12 && bytes[7] == 0xaa);

    memset(bytes, 0xaa, sizeof(bytes));
    WriteLEFloat(bytes + 1, 1.0f);
    REQUIRE(bytes[0] == 0xaa && bytes[1] == 0x00 && bytes[2] == 0x00 &&
            bytes[3] == 0x80 && bytes[4] == 0x3f && bytes[5] == 0xaa);

    memset(bytes, 0xaa, sizeof(bytes));
    WriteLE64(bytes + 1, 0x0123456789abcdefull);
    REQUIRE(bytes[0] == 0xaa && bytes[1] == 0xef && bytes[2] == 0xcd && bytes[3] == 0xab &&
            bytes[4] == 0x89 && bytes[5] == 0x67 && bytes[6] == 0x45 && bytes[7] == 0x23 &&
            bytes[8] == 0x01 && bytes[9] == 0xaa);
    REQUIRE(ReadLE64(bytes + 1) == 0x0123456789abcdefull);

    /* Literal little-endian input, read from an unaligned address. */
    {
        static const unsigned char raw16[] = {0xff, 0x34, 0x12};
        static const unsigned char raw32[] = {0xff, 0x78, 0x56, 0x34, 0x12};
        static const unsigned char rawf[] = {0xff, 0x00, 0x00, 0x80, 0x3f};
        REQUIRE(ReadLE16(raw16 + 1) == 0x1234);
        REQUIRE(ReadLE32(raw32 + 1) == 0x12345678u);
        REQUIRE(FloatBits(ReadLEFloat(rawf + 1)) == 0x3f800000u);
    }

    return 0;
}

static int TestUnalignedRoundTrips()
{
    for (size_t offset = 0; offset < 8; ++offset) {
        unsigned char buffer[32];

        {
            const uint16_t value = (uint16_t)(0x1200u + offset);
            memset(buffer, 0xcd, sizeof(buffer));
            WriteLE16(buffer + offset, value);
            REQUIRE(ReadLE16(buffer + offset) == value);
            if (offset > 0) REQUIRE(buffer[offset - 1] == 0xcd);
            REQUIRE(buffer[offset + 2] == 0xcd);
        }

        {
            const uint32_t value = 0x89abcdefu ^ (uint32_t)offset;
            memset(buffer, 0xcd, sizeof(buffer));
            WriteLE32(buffer + offset, value);
            REQUIRE(ReadLE32(buffer + offset) == value);
            if (offset > 0) REQUIRE(buffer[offset - 1] == 0xcd);
            REQUIRE(buffer[offset + 4] == 0xcd);
        }

        {
            const float value = -1.5f - (float)offset;
            memset(buffer, 0xcd, sizeof(buffer));
            WriteLE64(buffer + offset, 0xfedcba9876543210ull);
            REQUIRE(ReadLE64(buffer + offset) == 0xfedcba9876543210ull);
            if (offset > 0) REQUIRE(buffer[offset - 1] == 0xcd);
            REQUIRE(buffer[offset + 8] == 0xcd);

            memset(buffer, 0xcd, sizeof(buffer));
            WriteLEFloat(buffer + offset, value);
            REQUIRE(FloatBits(ReadLEFloat(buffer + offset)) == FloatBits(value));
            if (offset > 0) REQUIRE(buffer[offset - 1] == 0xcd);
            REQUIRE(buffer[offset + 4] == 0xcd);
        }
    }

    return 0;
}

/* Every bit pattern must survive, including NaN payloads and denormals. */
static int TestFloatBitPatterns()
{
    static const uint32_t patterns[] = {
        0x00000000u, 0x80000000u, 0x3f800000u, 0x7f800000u,
        0xff800000u, 0x7fc00001u, 0x00000001u, 0x7f7fffffu,
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        unsigned char bytes[8];
        const float value = FloatFromBits(patterns[i]);

        memset(bytes, 0xaa, sizeof(bytes));
        WriteLEFloat(bytes + 2, value);
        for (size_t byte = 0; byte < 4; ++byte)
            REQUIRE(bytes[2 + byte] == (unsigned char)(patterns[i] >> (8 * byte)));
        REQUIRE(FloatBits(ReadLEFloat(bytes + 2)) == patterns[i]);
    }

    return 0;
}

static int TestSwap()
{
    REQUIRE(Swap16(0x1234) == 0x3412);
    REQUIRE(Swap16(Swap16(0x1234)) == 0x1234);
    REQUIRE(Swap32(0x12345678u) == 0x78563412u);
    REQUIRE(Swap32(Swap32(0x12345678u)) == 0x12345678u);
    REQUIRE(Swap64(0x0123456789abcdefull) == 0xefcdab8967452301ull);
    REQUIRE(Swap64(Swap64(0x0123456789abcdefull)) == 0x0123456789abcdefull);

    {
        const float value = FloatFromBits(0x3f800000u);
        REQUIRE(FloatBits(SwapFloat(value)) == 0x0000803fu);
        REQUIRE(FloatBits(SwapFloat(SwapFloat(value))) == 0x3f800000u);
    }

    return 0;
}

static int TestArrays()
{
    const uint32_t words[3] = {0x01020304u, 0xdeadbeefu, 0u};
    unsigned char bytes[2 + 3 * 4 + 2];
    uint32_t read[3];

    memset(bytes, 0xee, sizeof(bytes));
    WriteLE32Array(bytes + 2, words, 3);
    REQUIRE(bytes[0] == 0xee && bytes[1] == 0xee && bytes[14] == 0xee && bytes[15] == 0xee);
    for (size_t i = 0; i < 3; ++i)
        for (size_t byte = 0; byte < 4; ++byte)
            REQUIRE(bytes[2 + i * 4 + byte] == (unsigned char)(words[i] >> (8 * byte)));

    memset(read, 0, sizeof(read));
    ReadLE32Array(read, bytes + 2, 3);
    REQUIRE(memcmp(read, words, sizeof(words)) == 0);

    {
        const uint16_t input[2] = {0xa1b2u, 0xc3d4u};
        uint16_t output[2] = {0, 0};
        unsigned char raw[1 + 2 * 2 + 1];

        memset(raw, 0x5a, sizeof(raw));
        WriteLE16Array(raw + 1, input, 2);
        REQUIRE(raw[0] == 0x5a && raw[5] == 0x5a);
        REQUIRE(raw[1] == 0xb2 && raw[2] == 0xa1 && raw[3] == 0xd4 && raw[4] == 0xc3);
        ReadLE16Array(output, raw + 1, 2);
        REQUIRE(memcmp(output, input, sizeof(input)) == 0);
    }

    {
        const float input[2] = {1.5f, -2.25f};
        float output[2] = {0.0f, 0.0f};
        unsigned char raw[2 + 2 * 4 + 2];

        memset(raw, 0x5a, sizeof(raw));
        WriteLEFloatArray(raw + 2, input, 2);
        REQUIRE(raw[0] == 0x5a && raw[1] == 0x5a && raw[10] == 0x5a && raw[11] == 0x5a);
        ReadLEFloatArray(output, raw + 2, 2);
        REQUIRE(FloatBits(output[0]) == FloatBits(input[0]));
        REQUIRE(FloatBits(output[1]) == FloatBits(input[1]));
    }

    /* count == 0 must not touch either buffer. */
    {
        unsigned char raw[2] = {0x11, 0x22};
        WriteLE16Array(raw, NULL, 0);
        WriteLE32Array(raw, NULL, 0);
        ReadLE16Array(NULL, raw, 0);
        REQUIRE(raw[0] == 0x11 && raw[1] == 0x22);
    }

    return 0;
}

int main()
{
    if (TestStoredByteOrder()) return 1;
    if (TestUnalignedRoundTrips()) return 1;
    if (TestFloatBitPatterns()) return 1;
    if (TestSwap()) return 1;
    if (TestArrays()) return 1;
    if (!EndianC_SelfTest()) {
        fprintf(stderr, "endian helpers failed when compiled as C\n");
        return 1;
    }
    if (!EndianForcedBigSelfTest()) {
        fprintf(stderr, "big-endian branch is not wired to Swap*\n");
        return 1;
    }
    return 0;
}
