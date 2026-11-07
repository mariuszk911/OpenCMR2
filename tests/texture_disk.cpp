// .dds header words and C3D texture records (src/port/texture_disk.cpp). The
// fixtures are written through WriteLE*, so the forced big-endian variant
// proves the swap path on the little-endian host.
#include "port/types.h"

#include "Texture.h"
#include "port/endian.h"
#include "port/texture_disk.h"

#include <cstdio>
#include <cstring>

namespace {

int Fail(const char *what, int line)
{
    std::printf("texture_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

int TestDds()
{
    DDSHeader header;

    std::memset(&header, 0, sizeof(header));
    WriteLE32(&header.dwFlags, 0x000a1007);
    WriteLE32(&header.dwWidth, 512);
    WriteLE32(&header.dwHeight, 256);
    WriteLE32(&header.dwLinearSize, 0x20000);
    WriteLE32(&header.ddpfPixelFormat.dwFourCC, 0x31545844);
    WriteLE32(&header.ddpfPixelFormat.dwRGBBitCount, 32);
    WriteLE32(&header.ddpfPixelFormat.dwRGBAlphaBitMask, 0xff000000u);
    DdsDisk_Decode(&header);
    CHECK(header.dwFlags == 0x000a1007);
    CHECK(header.dwWidth == 512 && header.dwHeight == 256);
    CHECK(header.dwLinearSize == 0x20000);
    CHECK(header.ddpfPixelFormat.dwFourCC == 0x31545844);
    CHECK(header.ddpfPixelFormat.dwRGBBitCount == 32);
    CHECK(header.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff000000u);
    return 0;
}

// A record of `count` slots at buffer+offset (odd offsets are unaligned).
void PutRecord(unsigned char *buffer, size_t offset, unsigned int count,
               const unsigned int *indices)
{
    unsigned char *rec = buffer + offset;
    unsigned int i;

    WriteLE16(rec + 0xc, (uint16_t)count);
    for (i = 0; i < count; i++)
        WriteLE16(rec + 0x14 + i * 8, (uint16_t)indices[i]);
}

int TestRecords()
{
    unsigned char buffer[0x100];
    const unsigned int indices[3] = {2, 0, 1};
    char names[3 * 0x104];
    unsigned int count = 99;
    unsigned int index = 99;
    char *name = NULL;
    unsigned char *rec = buffer + 1; // unaligned on purpose
    int i;

    std::memset(buffer, 0, sizeof(buffer));
    PutRecord(buffer, 1, 3, indices);
    CHECK(TextureRecord_Open(rec, NULL, &count) == TEXTURE_RECORD_OK && count == 3);
    CHECK(TextureRecord_Open(rec, buffer + sizeof(buffer), &count) == TEXTURE_RECORD_OK && count == 3);
    CHECK(TextureRecord_Next(rec, count) == rec + 0x10 + 3 * 8);

    // Exactly fitting and one byte short.
    CHECK(TextureRecord_Open(rec, rec + 0x10 + 3 * 8, &count) == TEXTURE_RECORD_OK);
    CHECK(TextureRecord_Open(rec, rec + 0x10 + 3 * 8 - 1, &count) == TEXTURE_RECORD_BAD_COUNT);
    CHECK(count == 3);
    // Header does not fit.
    CHECK(TextureRecord_Open(rec, rec + 0xf, &count) == TEXTURE_RECORD_OVERRUN && count == 0);
    CHECK(TextureRecord_Open(rec, rec, &count) == TEXTURE_RECORD_OVERRUN);
    CHECK(TextureRecord_Open(rec, rec - 1, &count) == TEXTURE_RECORD_OVERRUN);
    // Count limits.
    WriteLE16(rec + 0xc, 0x1000);
    CHECK(TextureRecord_Open(rec, NULL, &count) == TEXTURE_RECORD_OK && count == 0x1000);
    WriteLE16(rec + 0xc, 0x1001);
    CHECK(TextureRecord_Open(rec, NULL, &count) == TEXTURE_RECORD_BAD_COUNT && count == 0x1001);
    WriteLE16(rec + 0xc, 0);
    CHECK(TextureRecord_Open(rec, NULL, &count) == TEXTURE_RECORD_OK && count == 0);
    CHECK(TextureRecord_Next(rec, 0) == rec + 0x10);

    // Names.
    std::memset(names, 'A', sizeof(names));
    for (i = 0; i < 3; i++)
        std::snprintf(names + i * 0x104, 0x104, "NAME%d", i);
    PutRecord(buffer, 1, 3, indices);
    CHECK(TextureRecord_Name(rec, 0, names, 3, &name, &index) == TEXTURE_RECORD_OK);
    CHECK(index == 2 && name == names + 2 * 0x104 && std::strcmp(name, "NAME2") == 0);
    CHECK(TextureRecord_Name(rec, 1, names, 3, &name, &index) == TEXTURE_RECORD_OK);
    CHECK(index == 0 && name == names);
    CHECK(TextureRecord_Name(rec, 0, names, 2, &name, &index) == TEXTURE_RECORD_BAD_INDEX);
    CHECK(index == 2 && name == NULL);
    // A slot filled to the last byte has no terminator.
    std::memset(names + 1 * 0x104, 'B', 0x104);
    CHECK(TextureRecord_Name(rec, 2, names, 3, &name, &index) == TEXTURE_RECORD_UNTERMINATED);
    CHECK(index == 1 && name == NULL);
    names[1 * 0x104 + 0x103] = '\0';
    CHECK(TextureRecord_Name(rec, 2, names, 3, &name, &index) == TEXTURE_RECORD_OK);
    CHECK(std::strlen(name) == 0x103);

    CHECK(std::strcmp(TextureRecord_StatusText(TEXTURE_RECORD_OVERRUN), "ok") != 0);
    return 0;
}

} // namespace

int main()
{
    int failed = TestDds();

    if (failed == 0)
        failed = TestRecords();
    if (failed == 0)
        std::printf("texture_disk: OK\n");
    return failed;
}
