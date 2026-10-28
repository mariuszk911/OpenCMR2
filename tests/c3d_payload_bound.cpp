// Bounded C3D word conversion used for the .xhi member extent.
#include "port/c3d_payload.h"
#include "port/endian.h"

#include <stdio.h>
#include <string.h>

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
    printf("c3d_payload_bound: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

int main(void)
{
    unsigned char source[16];
    unsigned char output[16];
    int i;

    for (i = 0; i < 4; i++)
        PutLE32(source + i * 4, 0x1000u + i);

    memset(output, 0, sizeof(output));
    CHECK(C3D_DecodeWordsBounded(output, source, sizeof(source), sizeof(source)) != 0);
    for (i = 0; i < 4; i++)
        CHECK(GetHost32(output + i * 4) == 0x1000u + i);

    // A range larger than the member is rejected without writing.
    memset(output, 0x5a, sizeof(output));
    CHECK(C3D_DecodeWordsBounded(output, source, sizeof(source), sizeof(source) - 4) == 0);
    CHECK(output[0] == 0x5a);
    CHECK(C3D_DecodeWordsBounded(output, source, 6, sizeof(source)) == 0);
    CHECK(C3D_DecodeWordsBounded(NULL, source, sizeof(source), sizeof(source)) == 0);
    CHECK(C3D_DecodeWordsBounded(output, NULL, sizeof(source), sizeof(source)) == 0);

    printf("c3d_payload_bound: OK\n");
    return 0;
}
