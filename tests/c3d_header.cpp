// C3D ("PP_F") header fixture. The normal build decodes a little-endian
// header; the forced big-endian variant pre-swaps the fixture so the swapped
// branch must still produce the same values, proving the loader does not read
// the header as host integers.
#include "port/c3d_header.h"
#include "port/c3d_payload.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

void Put16(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 8) & 0xff);
    p[1] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
#endif
}

void Put32(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

void Fill(unsigned char *p)
{
    Put32(p + 0x00, 0x465f5050); // "PP_F" read as a little-endian word
    Put32(p + 0x04, 0x11223344);
    Put32(p + 0x08, 0x55667788);
    Put32(p + 0x0c, 0x00000050);
    Put32(p + 0x10, 0x00000020);
    Put32(p + 0x14, 0x00001b98);
    Put16(p + 0x18, 0x0004);
    Put16(p + 0x1a, 0x0006);
    Put16(p + 0x1c, 0x0002);
    Put16(p + 0x1e, 0x0009);
    Put32(p + 0x20, 0x0000ffff);
    Put16(p + 0x24, 0x0003);
    Put16(p + 0x26, 0x0005);
    Put16(p + 0x28, 0x0007);
    Put32(p + 0x2c, 0x01020304);
}

unsigned int Get16(const unsigned char *p)
{
    uint16_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

unsigned int Get32(const unsigned char *p)
{
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

void FillTriangle(unsigned char *p)
{
    Put16(p + 0x00, 0x1234);
    Put32(p + 0x04, 0x00000011);
    Put32(p + 0x04 + 9 * 4, 0xfffffffe);
    Put32(p + 0x2c, 0x00000003);
    Put32(p + 0x30, 0x00000004);
    p[0x34] = 0x12;
    p[0x35] = 0x34;
    p[0x36] = 0x56;
    p[0x37] = 0x78;
    Put16(p + 0x40, 0x0005);
    Put16(p + 0x42, 0x0006);
    Put16(p + 0x44, 0x0007);
}

bool Check(const C3DHeaderValues &h)
{
    return h.field0x0 == 0x465f5050 &&
           h.textureFile == 0x11223344 &&
           h.field0x8 == 0x55667788 &&
           h.textureNamesOffset == 0x50 &&
           h.vertexFlagCount == 0x20 &&
           h.vertexCount == 0x1b98 &&
           h.nodeCount == 4 &&
           h.meshCount == 6 &&
           h.objectCount == 2 &&
           h.sectorCount == 9 &&
           h.triangleCount == 0xffff &&
           h.textureCount == 3 &&
           h.recordCount == 5 &&
           h.partCount == 7 &&
           h.field0x2c == 0x01020304;
}

} // namespace

int main()
{
    static_assert(C3D_HEADER_SIZE == 0x30, "C3D header size");
    static_assert(C3D_TRIANGLE_SIZE == 0x4c, "C3D triangle size");

    unsigned char raw[C3D_HEADER_SIZE];
    unsigned char shifted[C3D_HEADER_SIZE + 1];
    C3DHeaderValues header;

    std::memset(raw, 0, sizeof(raw));
    Fill(raw);
    std::memset(&header, 0, sizeof(header));
    REQUIRE(C3DHeader_Decode(raw, &header) != 0);
    REQUIRE(Check(header));
    REQUIRE(C3DHeader_Decode(NULL, &header) == 0);

    std::memset(shifted, 0, sizeof(shifted));
    Fill(shifted + 1);
    std::memset(&header, 0, sizeof(header));
    REQUIRE(C3DHeader_Decode(shifted + 1, &header) != 0);
    REQUIRE(Check(header));

    unsigned char triangleFile[C3D_TRIANGLE_SIZE];
    unsigned char triangleNative[C3D_TRIANGLE_SIZE];
    std::memset(triangleFile, 0, sizeof(triangleFile));
    FillTriangle(triangleFile);
    C3D_DecodeTriangle(triangleNative, triangleFile);
    REQUIRE(Get16(triangleNative + 0x00) == 0x1234);
    REQUIRE(Get32(triangleNative + 0x04) == 0x00000011);
    REQUIRE(Get32(triangleNative + 0x04 + 9 * 4) == 0xfffffffe);
    REQUIRE(Get32(triangleNative + 0x2c) == 3);
    REQUIRE(Get32(triangleNative + 0x30) == 4);
    REQUIRE(triangleNative[0x34] == 0x12 && triangleNative[0x35] == 0x34 &&
            triangleNative[0x36] == 0x56 && triangleNative[0x37] == 0x78);
    REQUIRE(Get16(triangleNative + 0x40) == 5);
    REQUIRE(Get16(triangleNative + 0x42) == 6);
    REQUIRE(Get16(triangleNative + 0x44) == 7);

    unsigned char triangleShifted[C3D_TRIANGLE_SIZE + 1];
    std::memset(triangleShifted, 0, sizeof(triangleShifted));
    FillTriangle(triangleShifted + 1);
    std::memset(triangleNative, 0, sizeof(triangleNative));
    C3D_DecodeTriangle(triangleNative, triangleShifted + 1);
    REQUIRE(Get16(triangleNative + 0x00) == 0x1234);
    REQUIRE(Get32(triangleNative + 0x04) == 0x00000011);
    REQUIRE(Get32(triangleNative + 0x30) == 4);

    unsigned char wordsFile[8];
    unsigned char wordsNative[8];
    unsigned char wordsShifted[9];
    Put32(wordsFile, 0x3f800000);
    Put32(wordsFile + 4, 0xc2c80000);
    C3D_DecodeWords(wordsNative, wordsFile, sizeof(wordsFile));
    REQUIRE(Get32(wordsNative) == 0x3f800000);
    REQUIRE(Get32(wordsNative + 4) == 0xc2c80000);

    std::memset(wordsShifted, 0, sizeof(wordsShifted));
    Put32(wordsShifted + 1, 0x3f800000);
    Put32(wordsShifted + 5, 0xc2c80000);
    std::memset(wordsNative, 0, sizeof(wordsNative));
    C3D_DecodeWords(wordsNative, wordsShifted + 1, 8);
    REQUIRE(Get32(wordsNative) == 0x3f800000);
    REQUIRE(Get32(wordsNative + 4) == 0xc2c80000);

    return 0;
}
