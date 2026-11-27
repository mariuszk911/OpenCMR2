/*
 * frexp and ldexp on the IEEE 754 bits, for the WarpOS build.
 *
 * newlib's versions are generic C: ldexp goes through finite() and scalbn(),
 * frexp through word extraction macros, each a call. The game's x87 single
 * precision helpers (game/X87Single.h, X87s_Round) call both for every
 * emulated x87 operation, and a PC sample of a driving frame put the four
 * functions at 6% of the frame. These give the same results bit for bit:
 * a normal input whose result is normal only has its exponent field
 * changed; everything else (zero, subnormal, infinity, NaN, overflow,
 * underflow) takes the same path as newlib, including errno for ldexp.
 * The host test (tests/wos_fastmath.cpp) compares them with the C library.
 */
#ifndef OPENCMR2_WOS_FASTMATH_H
#define OPENCMR2_WOS_FASTMATH_H

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static inline uint64_t WosMath_Bits(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static inline double WosMath_FromBits(uint64_t bits)
{
    double x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

#define WOS_MATH_EXP_MASK (UINT64_C(0x7ff) << 52)

static inline double WosMath_Frexp(double x, int *exponent)
{
    uint64_t bits = WosMath_Bits(x);
    int field = (int)((bits >> 52) & 0x7ff);
    int bias = 1022;

    *exponent = 0;
    if (field == 0x7ff || (bits << 1) == 0)
        return x;   // infinity, NaN, zero: unchanged, exponent 0
    if (field == 0) {
        // Subnormal: scale by 2^54 (exact) to make it normal.
        bits = WosMath_Bits(x * 18014398509481984.0);
        field = (int)((bits >> 52) & 0x7ff);
        bias += 54;
    }
    *exponent = field - bias;
    return WosMath_FromBits((bits & ~WOS_MATH_EXP_MASK) | (UINT64_C(1022) << 52));
}

static inline double WosMath_Ldexp(double x, int exponent)
{
    const uint64_t bits = WosMath_Bits(x);
    const int field = (int)((bits >> 52) & 0x7ff);

    // Normal in, normal out: exact, no rounding, no errno.
    if (field != 0 && field != 0x7ff && exponent > -2046 && exponent < 2046) {
        const int result = field + exponent;
        if (result >= 1 && result <= 2046)
            return WosMath_FromBits((bits & ~WOS_MATH_EXP_MASK) | ((uint64_t)result << 52));
    }
    // Everything else as newlib's ldexp does it.
    if (!isfinite(x) || x == 0.0)
        return x;
    x = scalbn(x, exponent);
    if (!isfinite(x) || x == 0.0)
        errno = ERANGE;
    return x;
}

#endif
