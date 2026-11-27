// The WarpOS frexp/ldexp replacements must match the C library bit for bit,
// errno included, on normal, subnormal, zero, infinite and NaN inputs and on
// results that overflow or underflow.
#include "platform/warpos/wos_fastmath.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

static uint64_t Bits(double x)
{
    uint64_t b;
    memcpy(&b, &x, sizeof(b));
    return b;
}

static int s_failures;

static void CheckFrexp(double x)
{
    int e1 = 12345, e2 = 54321;
    const double r1 = frexp(x, &e1);
    const double r2 = WosMath_Frexp(x, &e2);
    const bool nan = std::isnan(x);
    if ((nan ? !std::isnan(r2) : Bits(r1) != Bits(r2)) || (!nan && !std::isinf(x) && e1 != e2)) {
        fprintf(stderr, "frexp(%a): libc %a,%d ours %a,%d\n", x, r1, e1, r2, e2);
        s_failures++;
    }
}

static void CheckLdexp(double x, int n)
{
    errno = 0;
    const double r1 = ldexp(x, n);
    const bool range1 = errno == ERANGE;
    errno = 0;
    const double r2 = WosMath_Ldexp(x, n);
    const bool range2 = errno == ERANGE;
    const bool same = std::isnan(x) ? std::isnan(r2) : Bits(r1) == Bits(r2);
    if (!same || range1 != range2) {
        fprintf(stderr, "ldexp(%a, %d): libc %a%s ours %a%s\n", x, n, r1, range1 ? " ERANGE" : "", r2,
                range2 ? " ERANGE" : "");
        s_failures++;
    }
}

int main()
{
    const double specials[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, 3.0, 1.0 / 3.0, -2.75, 1e-300, -1e300, 1e308,
        std::numeric_limits<double>::min(), -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min() / 3.0, std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
        16777216.0, 0.1f, 123456.789f, -0.000123f,
    };
    const int exponents[] = {
        0, 1, -1, 2, -2, 24, -24, 53, -53, 127, -127, 149, -149, 1022, -1022, 1023, -1023,
        1074, -1074, 1075, -1075, 2045, -2045, 2046, -2046, 2100, -2100, 100000, -100000,
        std::numeric_limits<int>::max(), std::numeric_limits<int>::min(),
    };
    for (double x : specials) {
        CheckFrexp(x);
        for (int n : exponents)
            CheckLdexp(x, n);
    }
    // A sweep over exponent fields and mantissas, both signs.
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 200000; i++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        uint64_t bits = seed;
        if (i % 4 == 0)
            bits &= ~(UINT64_C(0x7ff) << 52);   // subnormals and zeros
        double x;
        memcpy(&x, &bits, sizeof(x));
        CheckFrexp(x);
        CheckLdexp(x, (int)((seed >> 20) % 4300) - 2150);
        CheckLdexp(x, (int)((seed >> 40) % 64) - 32);
    }
    if (s_failures) {
        fprintf(stderr, "%d mismatches\n", s_failures);
        return 1;
    }
    printf("wos_fastmath: ok\n");
    return 0;
}
