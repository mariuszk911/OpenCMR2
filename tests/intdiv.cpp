#include "port/intdiv.h"

#include <climits>
#include <cmath>
#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int RefMulDiv(int a, int b, int divisor)
{
    return (int)(((long long)a * b) / divisor);
}

static int RefMulAddDiv(int a, int b, int add, int divisor)
{
    return (int)(((long long)a * b + add) / divisor);
}

static int RefRecip32(int divisor)
{
    return (int)((1LL << 32) / divisor);
}

static int RefMenuScale(int baseX, int realX, int realY)
{
    return (int)((long long)baseX * 3 / 4 * 100 * realX / ((long long)baseX * realY));
}

static int RefDoubleToInt64Low(double value)
{
    long long rounded;
    if (value >= -9223372036854775808.0 && value < 9223372036854775808.0)
        rounded = llrint(value);
    else
        rounded = (long long)(1ULL << 63);
    return (int)rounded;
}

int main()
{
    static const int values[] = {
        INT_MIN, -1000000, -65536, -641, -640, -1, 0, 1, 7, 639, 640, 641,
        65536, 1000000, INT_MAX
    };
    static const int divisors[] = {
        INT_MIN, -65536, -640, -7, -1, 1, 2, 3, 7, 640, 65536, INT_MAX
    };
    static const int adds[] = {INT_MIN, -1000000, -1, 0, 1, 640, 65536, INT_MAX};

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (size_t j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            for (size_t k = 0; k < sizeof(divisors) / sizeof(divisors[0]); ++k) {
                const int a = values[i];
                const int b = values[j];
                const int d = divisors[k];
                REQUIRE(Port_MulDiv(a, b, d) == RefMulDiv(a, b, d));
                for (size_t m = 0; m < sizeof(adds) / sizeof(adds[0]); ++m) {
                    const int add = adds[m];
                    REQUIRE(Port_MulAddDiv(a, b, add, d) == RefMulAddDiv(a, b, add, d));
                }
            }
        }
        for (size_t k = 0; k < sizeof(divisors) / sizeof(divisors[0]); ++k)
            REQUIRE(Port_Recip32(divisors[k]) == RefRecip32(divisors[k]));
    }

    {
        static const double samples[] = {
            0.0, 0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 1.0, -1.0,
            2147483647.0, 2147483648.0, -2147483648.0, -2147483649.0,
            4294967296.0, -4294967296.0, 1e18, -1e18, 1e30, -1e30,
            9223372036854775807.0
        };
        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
            REQUIRE(Port_DoubleToInt64Low(samples[i]) == RefDoubleToInt64Low(samples[i]));
    }

    {
        static const int baseX[] = {640, 1024};
        static const int widths[] = {320, 640, 800, 1024, 1280, 1920, 2560, 3840};
        static const int heights[] = {200, 240, 480, 600, 720, 768, 1080, 1600, 2160};
        for (size_t i = 0; i < sizeof(baseX) / sizeof(baseX[0]); ++i) {
            for (size_t j = 0; j < sizeof(widths) / sizeof(widths[0]); ++j) {
                for (size_t k = 0; k < sizeof(heights) / sizeof(heights[0]); ++k) {
                    REQUIRE(Port_MenuScale(baseX[i], widths[j], heights[k]) ==
                            RefMenuScale(baseX[i], widths[j], heights[k]));
                }
            }
        }
    }

    return 0;
}
