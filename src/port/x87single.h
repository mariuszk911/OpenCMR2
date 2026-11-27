/*
 * x87 single-precision arithmetic (game/X87Single.h), with a fast path.
 *
 * game/X87Single.h emulates the x87 with its precision control at 24 bits:
 * every add, subtract, multiply, divide and square root rounds the exact
 * result once to a 24-bit mantissa, keeping the 15-bit exponent. For each
 * operation it computes the exact rest with an error-free transformation
 * (Dekker's product, two-sum) and rounds through frexp, a float conversion
 * and ldexp. That is bit-exact, but on the Amiga (libnix frexp/ldexp, each
 * with a memcpy) it costs about 200 instructions, ~1 us under Emu68. The tyre
 * marks alone do ~18,000 of these per frame (FIN01 bench: ~20 ms/frame).
 *
 * Upstream's X87s_Round consults the exact rest (lo) only when frexp(hi)
 * lies exactly halfway between two floats, that is when hi's low 29 mantissa
 * bits are one half; for every other normal hi it returns hi rounded to
 * nearest even at 24 bits. The fast path computes that from hi's bits.
 * Ties, zeros, denormals, infinities and NaNs (~0.5 % of the game's
 * operations) take upstream's code below unchanged, so every result is
 * bit-identical to it (tests/x87_single.cpp compares the two).
 *
 * This header defines game/X87Single.h's include guard and is force-included
 * (through port/msvcrt.h) before any game source, so game/ stays identical to
 * upstream. The exact path below is a copy of upstream's code and the macros
 * repeat upstream's; after an upstream sync, diff them against
 * game/X87Single.h (the test compares behaviour on sampled inputs only).
 */
#ifndef _X87SINGLE_H
#if !(defined(_MSC_VER) && _MSC_VER < 1300) /* MSVC6 builds the original: game/X87Single.h's plain operators */
#define _X87SINGLE_H
#define OPENCMR2_PORT_X87SINGLE_H

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ---- upstream (game/X87Single.h), unchanged: the exact path ---------------- */

/* Rounds an exact result to a 24-bit mantissa, nearest even. hi is the double
   nearest to it and lo has the sign of the rest (exact result - hi). */
static inline double X87s_Round(double hi, double lo)
{
    int exponent;
    double m, rounded, other;
    float r;

    if (hi == 0.0 || !isfinite(hi))
        return hi;
    /* Scale into [0.5, 1): the x87 exponent range does not overflow a float. */
    m = frexp(hi, &exponent);
    r = (float)m;
    rounded = r;
    if (rounded != m && lo != 0.0) {
        /* When hi is halfway between two floats, the rest decides. */
        other = nextafterf(r, m > rounded ? 2.0f : -2.0f);
        if (fabs(m - rounded) == fabs(other - m))
            rounded = lo > 0.0 ? fmax(rounded, other) : fmin(rounded, other);
    }
    return ldexp(rounded, exponent);
}

/* Dekker's exact product: a * b == *pHi + *pLo. */
static inline void X87s_TwoProduct(double a, double b, double *pHi, double *pLo)
{
    const double split = 134217729.0; /* 2^27 + 1 */
    double c, aHi, aLo, bHi, bLo;

    *pHi = a * b;
    c = split * a;
    aHi = c - (c - a);
    aLo = a - aHi;
    c = split * b;
    bHi = c - (c - b);
    bLo = b - bHi;
    *pLo = ((aHi * bHi - *pHi) + aHi * bLo + aLo * bHi) + aLo * bLo;
}

/* ---- the fast path ----------------------------------------------------------- */

/* Stores hi rounded to a 24-bit mantissa (nearest even) and returns 1 when
   hi alone decides the rounding; returns 0 for an exact tie, a zero, a
   denormal, an infinity or a NaN, where the caller runs the exact path. */
static inline int X87s_RoundFast(double hi, double *out)
{
    uint64_t bits;
    uint32_t exponent, low;

    memcpy(&bits, &hi, sizeof(bits));
    exponent = (uint32_t)(bits >> 52) & 0x7ffu;
    low = (uint32_t)bits & 0x1fffffffu;
    if (exponent == 0 || exponent == 0x7ffu || low == 0x10000000u)
        return 0;
    if (low != 0) {
        bits -= low;
        if (low > 0x10000000u)
            bits += 0x20000000u; /* a carry into the exponent is the IEEE result */
    }
    memcpy(out, &bits, sizeof(bits));
    return 1;
}

static inline double X87s_Add(double a, double b)
{
    double s = a + b, r, bb;

    if (X87s_RoundFast(s, &r))
        return r;
    bb = s - a;
    return X87s_Round(s, (a - (s - bb)) + (b - bb));
}

static inline double X87s_Mul(double a, double b)
{
    double hi, lo;

    if (X87s_RoundFast(a * b, &hi))
        return hi;
    X87s_TwoProduct(a, b, &hi, &lo);
    return X87s_Round(hi, lo);
}

static inline double X87s_Div(double a, double b)
{
    double q = a / b, r;
    double hi, lo;

    if (X87s_RoundFast(q, &r))
        return r;
    /* The rest of the quotient has the sign of (a - q * b) / b. */
    X87s_TwoProduct(q, b, &hi, &lo);
    return X87s_Round(q, ((a - hi) - lo) * (b < 0.0 ? -1.0 : 1.0));
}

static inline double X87s_Sqrt(double x)
{
    double s = sqrt(x), r;
    double hi, lo;

    if (X87s_RoundFast(s, &r))
        return r;
    X87s_TwoProduct(s, s, &hi, &lo);
    return X87s_Round(s, (x - hi) - lo);
}

#define X87S_ADD(a, b) X87s_Add((double)(a), (double)(b))
#define X87S_SUB(a, b) X87s_Add((double)(a), -(double)(b))
#define X87S_MUL(a, b) X87s_Mul((double)(a), (double)(b))
#define X87S_DIV(a, b) X87s_Div((double)(a), (double)(b))
#define X87S_SQRT(x) X87s_Sqrt((double)(x))
#define X87S_FILD(type, x) ((double)(x))
#define X87S_LOAD(x) ((double)(x))

#endif
#endif
