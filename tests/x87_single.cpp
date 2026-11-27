// The fast path of src/port/x87single.h against upstream's game/X87Single.h:
// every X87S_* operation must give the same bits. The port header takes
// over game/X87Single.h's include guard, so upstream's code is compiled here
// in a namespace of its own and the two are compared through their X87S_*
// macros, as the game calls them, on random operands of the kinds the game
// uses (16.16 fixed-point integers, floats, small integers, doubles), on raw
// bit patterns, on exact ties at 24 bits with and without a rest below half a
// double ulp (the only case where the exact rest decides), and on zeros,
// denormals, infinities and NaNs. A behaviour change upstream fails here.
//
// Built for the host (ctest) and for the Amiga (amiga_x87_single_test,
// src/amiga.cmake), where the FPU under test is Emu68's.
#include "port/x87single.h"

// The port's operations as the game uses them: through the X87S_* macros.
static double PortAdd(double a, double b) { return X87S_ADD(a, b); }
static double PortSub(double a, double b) { return X87S_SUB(a, b); }
static double PortMul(double a, double b) { return X87S_MUL(a, b); }
static double PortDiv(double a, double b) { return X87S_DIV(a, b); }
static double PortSqrt(double x) { return X87S_SQRT(x); }
static double PortFild(int i) { return X87S_FILD(float, i); }
static double PortLoad(int i) { return X87S_LOAD(i); }

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
// Upstream's operations through upstream's macros (which name the functions
// of this namespace here).
static double Add(double a, double b) { return X87S_ADD(a, b); }
static double Sub(double a, double b) { return X87S_SUB(a, b); }
static double Mul(double a, double b) { return X87S_MUL(a, b); }
static double Div(double a, double b) { return X87S_DIV(a, b); }
static double Sqrt(double x) { return X87S_SQRT(x); }
static double Fild(int i) { return X87S_FILD(float, i); }
static double Load(int i) { return X87S_LOAD(i); }
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
    Check("add", a, b, PortAdd(a, b), upstream::Add(a, b));
    Check("sub", a, b, PortSub(a, b), upstream::Sub(a, b));
    Check("mul", a, b, PortMul(a, b), upstream::Mul(a, b));
    Check("div", a, b, PortDiv(a, b), upstream::Div(a, b));
    Check("sqrt", a, 0.0, PortSqrt(a), upstream::Sqrt(a));
    Check("sqrt", fabs(a), 0.0, PortSqrt(fabs(a)), upstream::Sqrt(fabs(a)));
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
    case 5: // any finite double
        return ldexp((double)(int64_t)Next(), -63 + (int)(Next() % 64) - 32);
    default: // any bit pattern: denormals, infinities, NaNs included
        return FromBits(Next());
    }
}

// An exact tie at 24 bits and an addend below half a double ulp of it, so
// that the tie meets a nonzero rest (the one case the exact path decides).
void CheckTieWithRest()
{
    const double tie = Operand(4);
    int exponent;
    frexp(tie, &exponent);
    const double rest = ldexp((Next() & 1) ? 1.0 : -1.0, exponent - 54 - (int)(Next() % 8));
    CheckPair(tie, rest);
    CheckPair(rest, tie);
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
        const int ka = (int)(Next() % 7), kb = (int)(Next() % 7);
        CheckPair(Operand(ka), Operand(kb));
        if ((i & 7) == 0)
            CheckTieWithRest();
        const int n = (int)Next();
        Check("fild", (double)n, 0.0, PortFild(n), upstream::Fild(n));
        Check("load", (double)n, 0.0, PortLoad(n), upstream::Load(n));
    }
    printf("x87_single: %ld checks, %ld mismatches\n", s_checks, s_fails);
    return s_fails != 0;
}
