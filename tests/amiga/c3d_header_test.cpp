// Real 68060 big-endian/alignment coverage for the C3D header and payload.
// The fixture contains little-endian file bytes and is decoded by the target;
// the host forced-swap test only exercises the same branch on x86.
#include "port/c3d_header.h"
#include "port/c3d_payload.h"

#include <stdio.h>
#include <string.h>

static void PutLE16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
}

static void PutLE32(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
}

static unsigned int GetHost16(const unsigned char *p)
{
    unsigned short value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("c3d_header: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

int main(void)
{
    unsigned char header[C3D_HEADER_SIZE];
    unsigned char shifted[C3D_HEADER_SIZE + 1];
    C3DHeaderValues decoded;
    unsigned char triangleFile[C3D_TRIANGLE_SIZE];
    unsigned char triangleShifted[C3D_TRIANGLE_SIZE + 1];
    unsigned char triangleNative[C3D_TRIANGLE_SIZE];
    unsigned char wordsFile[8];
    unsigned char wordsShifted[9];
    unsigned char wordsNative[8];

    memset(header, 0, sizeof(header));
    PutLE32(header + 0x00, 0x465f5050);
    PutLE32(header + 0x04, 0x11223344);
    PutLE32(header + 0x08, 0x55667788);
    PutLE32(header + 0x0c, 0x50);
    PutLE32(header + 0x10, 0x20);
    PutLE32(header + 0x14, 0x1b98);
    PutLE16(header + 0x18, 4);
    PutLE16(header + 0x1a, 6);
    PutLE16(header + 0x1c, 2);
    PutLE16(header + 0x1e, 9);
    PutLE32(header + 0x20, 0xffff);
    PutLE16(header + 0x24, 3);
    PutLE16(header + 0x26, 5);
    PutLE16(header + 0x28, 7);
    PutLE32(header + 0x2c, 0x01020304);

    CHECK(C3DHeader_Decode(header, &decoded) != 0);
    CHECK(decoded.field0x0 == 0x465f5050);
    CHECK(decoded.textureFile == 0x11223344);
    CHECK(decoded.field0x8 == 0x55667788);
    CHECK(decoded.textureNamesOffset == 0x50);
    CHECK(decoded.vertexFlagCount == 0x20);
    CHECK(decoded.vertexCount == 0x1b98);
    CHECK(decoded.nodeCount == 4);
    CHECK(decoded.meshCount == 6);
    CHECK(decoded.objectCount == 2);
    CHECK(decoded.sectorCount == 9);
    CHECK(decoded.triangleCount == 0xffff);
    CHECK(decoded.textureCount == 3);
    CHECK(decoded.recordCount == 5);
    CHECK(decoded.partCount == 7);
    CHECK(decoded.field0x2c == 0x01020304);
    CHECK(C3DHeader_Decode(NULL, &decoded) == 0);

    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 1, header, sizeof(header));
    CHECK(C3DHeader_Decode(shifted + 1, &decoded) != 0);
    CHECK(decoded.nodeCount == 4);
    CHECK(decoded.textureFile == 0x11223344);

    memset(triangleFile, 0, sizeof(triangleFile));
    PutLE16(triangleFile + 0x00, 0x1234);
    PutLE32(triangleFile + 0x04, 0x11);
    PutLE32(triangleFile + 0x04 + 9 * 4, 0xfffffffe);
    PutLE32(triangleFile + 0x2c, 3);
    PutLE32(triangleFile + 0x30, 4);
    triangleFile[0x34] = 0x12;
    triangleFile[0x35] = 0x34;
    triangleFile[0x36] = 0x56;
    triangleFile[0x37] = 0x78;
    PutLE16(triangleFile + 0x40, 5);
    PutLE16(triangleFile + 0x42, 6);
    PutLE16(triangleFile + 0x44, 7);

    C3D_DecodeTriangle(triangleNative, triangleFile);
    CHECK(GetHost16(triangleNative + 0x00) == 0x1234);
    CHECK(GetHost32(triangleNative + 0x04) == 0x11);
    CHECK(GetHost32(triangleNative + 0x04 + 9 * 4) == 0xfffffffe);
    CHECK(GetHost32(triangleNative + 0x2c) == 3);
    CHECK(GetHost32(triangleNative + 0x30) == 4);
    CHECK(triangleNative[0x34] == 0x12 && triangleNative[0x35] == 0x34 &&
          triangleNative[0x36] == 0x56 && triangleNative[0x37] == 0x78);
    CHECK(GetHost16(triangleNative + 0x40) == 5);
    CHECK(GetHost16(triangleNative + 0x42) == 6);
    CHECK(GetHost16(triangleNative + 0x44) == 7);

    memset(triangleShifted, 0, sizeof(triangleShifted));
    memcpy(triangleShifted + 1, triangleFile, sizeof(triangleFile));
    memset(triangleNative, 0, sizeof(triangleNative));
    C3D_DecodeTriangle(triangleNative, triangleShifted + 1);
    CHECK(GetHost32(triangleNative + 0x30) == 4);

    PutLE32(wordsFile, 0x3f800000);
    PutLE32(wordsFile + 4, 0xc2c80000);
    C3D_DecodeWords(wordsNative, wordsFile, sizeof(wordsFile));
    CHECK(GetHost32(wordsNative) == 0x3f800000);
    CHECK(GetHost32(wordsNative + 4) == 0xc2c80000);

    memset(wordsShifted, 0, sizeof(wordsShifted));
    memcpy(wordsShifted + 1, wordsFile, sizeof(wordsFile));
    memset(wordsNative, 0, sizeof(wordsNative));
    C3D_DecodeWords(wordsNative, wordsShifted + 1, 8);
    CHECK(GetHost32(wordsNative) == 0x3f800000);
    CHECK(GetHost32(wordsNative + 4) == 0xc2c80000);

    printf("c3d_header: OK\n");
    return 0;
}
