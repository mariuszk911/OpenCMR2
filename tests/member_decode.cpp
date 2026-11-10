// MemberDecode_Find: archive members with little-endian records come back as
// native copies owned by the archive's buffer; other names are untouched; a
// malformed member is reported as missing.
#include "port/member_decode.h"

#include "port/disk.h"
#include "port/endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void Sys_Log(const char *, ...) {}

static void PutLE32(unsigned char *p, unsigned int value)
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

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("member_decode: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

// The archive's buffer: members are laid out one after another.
static const size_t kArchiveSize = 1024;

int main()
{
    unsigned char *archive = (unsigned char *)calloc(1, kArchiveSize);
    unsigned char *member = archive + 16;
    unsigned char *native;
    int i;

    CHECK(archive != NULL);
    Disk_RegisterBuffer(archive, kArchiveSize);

    // No decoder for the name: the member itself, untouched.
    memset(member, 0xAB, 32);
    CHECK(MemberDecode_Find("x.wav", member, 32) == member);
    CHECK(MemberDecode_Find("x.c3d", member, 32) == member);
    CHECK(MemberDecode_Find("cod", member, 32) == member);   // no extension
    CHECK(MemberDecode_Find(NULL, member, 32) == member);
    CHECK(MemberDecode_Find("x.cod", NULL, 32) == NULL);

    // .cod: little-endian 32-bit words.
    for (i = 0; i < 4; i++)
        PutLE32(member + i * 4, 0x11223344u + (unsigned)i);
    native = (unsigned char *)MemberDecode_Find("0100.cod", member, 16);
    CHECK(native != NULL && native != member);
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(native + i * 4) == 0x11223344u + (unsigned)i);
    CHECK(MemberDecode_Find("0100.cod", member, 0) == NULL);
    CHECK(MemberDecode_Find("0100.cod", member, 15) == NULL);   // not whole words

    // .tsc: a count, then 0x6c-byte records.
    memset(member, 0, 4 + 2 * 0x6c);
    PutLE32(member, 2);
    PutLE32(member + 4 + 0x40, 0xCAFE0001u);
    PutLE32(member + 4 + 0x6c + 0x40, 0xCAFE0002u);
    native = (unsigned char *)MemberDecode_Find("0100.tsc", member, 4 + 2 * 0x6c);
    CHECK(native != NULL && native != member);
    CHECK(GetHost32(native) == 2u);
    CHECK(GetHost32(native + 4 + 0x40) == 0xCAFE0001u);
    CHECK(GetHost32(native + 4 + 0x6c + 0x40) == 0xCAFE0002u);
    PutLE32(member, 3);                                          // count larger than the member
    CHECK(MemberDecode_Find("0100.tsc", member, 4 + 2 * 0x6c) == NULL);
    CHECK(MemberDecode_Find("0100.tsc", member, 3) == NULL);

    // .csp: start table, 32-bit words, with 16-bit words at 0x18.
    memset(member, 0, 0x38);
    PutLE32(member, 0x01020304u);
    PutLE32(member + 0x14, 0x05060708u);
    PutLE32(member + 0x20, 0x090A0B0Cu);
    PutLE32(member + 0x34, 0x0D0E0F10u);
    native = (unsigned char *)MemberDecode_Find("0100.csp", member, 0x38);
    CHECK(native != NULL && native != member);
    CHECK(GetHost32(native) == 0x01020304u);
    CHECK(GetHost32(native + 0x14) == 0x05060708u);
    CHECK(GetHost32(native + 0x20) == 0x090A0B0Cu);
    CHECK(GetHost32(native + 0x34) == 0x0D0E0F10u);
    CHECK(MemberDecode_Find("0100.csp", member, 4) == NULL);

    // .bsp: twelve ints (four vectors).
    memset(member, 0, 0x30);
    for (i = 0; i < 12; i++)
        PutLE32(member + i * 4, 0x00010000u * (unsigned)(i + 1));
    native = (unsigned char *)MemberDecode_Find("0100.bsp", member, 0x30);
    CHECK(native != NULL && native != member);
    for (i = 0; i < 12; i++)
        CHECK(GetHost32(native + i * 4) == 0x00010000u * (unsigned)(i + 1));
    CHECK(MemberDecode_Find("0100.bsp", member, 0x2c) == NULL);

    // .hor: a lighting preset, 32-bit words then bytes.
    memset(member, 0, 0x4e);
    PutLE32(member, 0x00020000u);
    PutLE32(member + 0x18, 0x00030000u);
    member[0x1c] = 0x5A;
    member[0x4d] = 0xA5;
    native = (unsigned char *)MemberDecode_Find("1200bli.hor", member, 0x4e);
    CHECK(native != NULL && native != member);
    CHECK(GetHost32(native) == 0x00020000u);
    CHECK(GetHost32(native + 0x18) == 0x00030000u);
    CHECK(native[0x1c] == 0x5A && native[0x4d] == 0xA5);
    CHECK(MemberDecode_Find("1200bli.hor", member, 0x4d) == NULL);

    // The input is never modified in place: it still holds the file bytes.
    CHECK(member[0x1c] == 0x5A);

    // The native copies belong to the archive: releasing it frees them.
    Disk_ReleaseBuffer(archive);
    free(archive);

    printf("member_decode: OK\n");
    return 0;
}
