// FTOI (game/FloatToInt.h) in a CMR2_QIFIST unit must give the x87 FISTP
// result of the game's control word 0x027f: round to nearest, ties to even.
// tests/ftoi_fistp.cpp checks that against the real instruction on x86; this
// test checks the same contract where the conversion is llrint, including the
// Amiga target, whose hard-float llrint is implemented in
// src/platform/amiga/amiga_libfix.c.
//
// The FixAcos cases are the ones that broke the attract replay: each of those
// dot products produces an exact 24-bit mantissa half, so a llrint that rounds
// ties away from zero returned the neighbouring acos index (0x1680 apart in
// the corner friction strength) and the car left the recorded line halfway
// through the first long corner.
#define CMR2_QIFIST
#include "FloatToInt.h"

#include <stdio.h>

static int fails;

// libnix printf is not trusted with %lld here, so report the two halves.
static void Check(long long got, long long want, const char *what, long long input)
{
    if (got != want) {
        printf("ftoi_ties: FAIL %s input=%08lx:%08lx got=%08lx:%08lx want=%08lx:%08lx\n", what,
               (unsigned long)((unsigned long long)input >> 32),
               (unsigned long)((unsigned long long)input & 0xffffffffUL),
               (unsigned long)((unsigned long long)got >> 32),
               (unsigned long)((unsigned long long)got & 0xffffffffUL),
               (unsigned long)((unsigned long long)want >> 32),
               (unsigned long)((unsigned long long)want & 0xffffffffUL));
        fails++;
    }
}

// Keeps the compiler from folding the call with its own idea of llrint.
static long long Lrint(double x)
{
    volatile double v = x;
    return llrint(v);
}

// FixAcos (game/Car.h): t = X87S_MUL(X87S_MUL(along, 1/65536), -4095.0), then
// FTOI(__int64, t) indexes g_acosTable. Every along below yields an exact
// half; the expected value is the even neighbour.
struct AcosHalf {
    int along;
    long long index;
};

static const AcosHalf acosHalves[] = {
    {20477, -1280}, {24574, -1536}, {28671, -1792}, {32768, -2048},
    {36865, -2304}, {40962, -2560}, {45059, -2816}, {49156, -3072},
    {53253, -3328}, {57350, -3584}, {61447, -3840}, {65528, -4094},
};

int main(void)
{
    unsigned i;
    int n;

    for (i = 0; i < sizeof(acosHalves) / sizeof(acosHalves[0]); i++) {
        double t = X87S_MUL(X87S_MUL((double)acosHalves[i].along, 1.0 / 65536.0), -4095.0);
        Check(Lrint(t), acosHalves[i].index, "FixAcos half", acosHalves[i].along);
        Check((long long)FTOI(__int64, t), acosHalves[i].index, "FTOI FixAcos half",
              acosHalves[i].along);
        Check(Lrint(-t), -acosHalves[i].index, "FixAcos half (negated)", -acosHalves[i].along);
    }

    // Plain ties: the even neighbour wins.
    Check(Lrint(0.5), 0, "llrint(0.5)", 0);
    Check(Lrint(1.5), 2, "llrint(1.5)", 1);
    Check(Lrint(2.5), 2, "llrint(2.5)", 2);
    Check(Lrint(-0.5), 0, "llrint(-0.5)", 0);
    Check(Lrint(-1.5), -2, "llrint(-1.5)", -1);
    Check(Lrint(-2.5), -2, "llrint(-2.5)", -2);
    Check(Lrint(4503599627370495.5), 4503599627370496LL, "llrint(2^52-0.5)", 0);
    // Just below a half must not round up.
    Check(Lrint(0.49999999999999994), 0, "llrint(0.49999999999999994)", 0);

    for (n = -20000; n <= 20000; n++) {
        long long even = (n % 2 == 0) ? n : (long long)n + 1;
        if (Lrint((double)n) != n) {
            Check(Lrint((double)n), n, "llrint(n)", n);
        }
        if (Lrint((double)n + 0.5) != even) {
            Check(Lrint((double)n + 0.5), even, "llrint(n.5)", n);
        }
        if (Lrint(-((double)n + 0.5)) != -even) {
            Check(Lrint(-((double)n + 0.5)), -even, "llrint(-n.5)", n);
        }
        if ((long long)FTOI(__int64, (double)n + 0.5) != even) {
            Check((long long)FTOI(__int64, (double)n + 0.5), even, "FTOI(n.5)", n);
        }
    }

    // Out of range stores the x87 integer indefinite, as Ftoi_Fist64 does.
    Check(Lrint(1e300), (long long)0x8000000000000000ULL, "llrint(1e300)", 0);
    Check(Lrint(-1e300), (long long)0x8000000000000000ULL, "llrint(-1e300)", 0);
    Check((long long)FTOI(__int64, 1e300), (long long)0x8000000000000000ULL, "FTOI(1e300)", 0);

    // 32-bit conversions: the tie rounds up to 2^31, which is out of range, so
    // Ftoi_Fist32 stores the integer indefinite just as FISTP does.
    Check((long long)FTOI(int, -2147483647.5), -2147483648LL, "FTOI(int, -2^31+0.5)", 0);
    Check((long long)FTOI(int, 2147483647.5), -2147483648LL, "FTOI(int, 2^31-0.5)", 0);

    if (fails != 0) {
        printf("ftoi_ties: %d failure(s)\n", fails);
        return 1;
    }
    printf("ftoi_ties: llrint and FTOI round to nearest, ties to even\n");
    return 0;
}
