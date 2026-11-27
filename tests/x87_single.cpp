// The fast path of src/port/x87single.h against upstream's game/X87Single.h:
// every X87S_* operation must give the same bits. The port header takes
// over game/X87Single.h's include guard, so upstream's code is compiled here
// in a namespace of its own and the two are compared on random operands of
// the kinds the game uses (16.16 fixed-point integers, floats, small
// integers, doubles), on operands built to give exact ties at 24 bits (the
// only case where the exact rest decides), and on zeros, denormals,
// infinities and NaNs. A changed upstream file fails here too.
//
// Built for the host (ctest) and for the Amiga (amiga_x87_single_test,
// src/amiga.cmake), where the FPU under test is Emu68's.
#include "port/x87single.h"

#undef _X87SINGLE_H
#undef X87S_ADD
#undef X87S_SUB
#undef X87S_MUL
#undef X87S_DIV
#undef X87S_SQRT
#undef X87S_FILD
#undef X87S_LOAD
namespace upstream {
#include "X87Single.h"
}

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

uint64_t s_state = 0x9e3779b97f4a7c15ull;
uint64_t Next()
{
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return s_state;
}

uint64_t Bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

double FromBits(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof(d));
    return d;
}

long s_checks, s_fails;

// libnix printf is not trusted with %llx, so report the halves.
void Check(const char *op, double a, double b, double got, double want)
{
    s_checks++;
    if (Bits(got) == Bits(want) || (isnan(got) && isnan(want)))
        return;
    if (s_fails++ < 10)
        printf("x87_single: FAIL %s a=%08lx:%08lx b=%08lx:%08lx got=%08lx:%08lx want=%08lx:%08lx\n", op,
               (unsigned long)(Bits(a) >> 32), (unsigned long)(Bits(a) & 0xffffffffu),
               (unsigned long)(Bits(b) >> 32), (unsigned long)(Bits(b) & 0xffffffffu),
               (unsigned long)(Bits(got) >> 32), (unsigned long)(Bits(got) & 0xffffffffu),
               (unsigned long)(Bits(want) >> 32), (unsigned long)(Bits(want) & 0xffffffffu));
}

void CheckPair(double a, double b)
{
    Check("add", a, b, X87s_Add(a, b), upstream::X87s_Add(a, b));
    Check("sub", a, b, X87s_Add(a, -b), upstream::X87s_Add(a, -b));
    Check("mul", a, b, X87s_Mul(a, b), upstream::X87s_Mul(a, b));
    Check("div", a, b, X87s_Div(a, b), upstream::X87s_Div(a, b));
    Check("sqrt", a, 0.0, X87s_Sqrt(a), upstream::X87s_Sqrt(a));
    Check("sqrt", fabs(a), 0.0, X87s_Sqrt(fabs(a)), upstream::X87s_Sqrt(fabs(a)));
}

double Operand(int kind)
{
    switch (kind) {
    case 0: // a 16.16 fixed-point value as the game converts it
        return (double)(int32_t)Next();
    case 1: // a float
        return (double)(float)ldexp((double)(int64_t)Next(), -40 - (int)(Next() % 24));
    case 2: // a small integer
        return (double)(int32_t)(Next() & 0xffff) - 32768.0;
    case 3: // the game's fixed-point scale constants
        return (Next() & 1) ? 1.0 / 65536.0 : 65536.0;
    case 4: // a double that is an exact tie at 24 bits
        return FromBits((Next() & ~(uint64_t)0x1fffffff & ~((uint64_t)0x7ff << 52)) | ((uint64_t)(1000 + Next() % 46) << 52) |
                        0x10000000u);
    default: // any finite double
        return ldexp((double)(int64_t)Next(), -63 + (int)(Next() % 64) - 32);
    }
}

} // namespace

int main(int argc, char **argv)
{
    const long count = argc > 1 ? atol(argv[1]) : 2000000;
    const double special[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 3.0, 1.0 / 65536.0, 65536.0,
                               4.9406564584124654e-324, 2.2250738585072014e-308, 1.7976931348623157e308,
                               3.4028234663852886e38, 1.1754943508222875e-38, 1.401298464324817e-45,
                               HUGE_VAL, -HUGE_VAL, NAN, 16777217.0, 33554435.0, 0.1 };
    const int ns = (int)(sizeof(special) / sizeof(special[0]));
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            CheckPair(special[i], special[j]);
    for (long i = 0; i < count; i++) {
        const int ka = (int)(Next() % 6), kb = (int)(Next() % 6);
        CheckPair(Operand(ka), Operand(kb));
    }
    printf("x87_single: %ld checks, %ld mismatches\n", s_checks, s_fails);
    return s_fails != 0;
}
