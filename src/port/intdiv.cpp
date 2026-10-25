// 64-bit-intermediate helpers, see port/intdiv.h.
//
// This file is Domain B: it must never be compiled with -malign-int.

#include "port/intdiv.h"

#include <math.h>

int Port_MulDiv(int a, int b, int divisor)
{
    return (int)(((long long)a * b) / divisor);
}

int Port_MulAddDiv(int a, int b, int add, int divisor)
{
    return (int)(((long long)a * b + add) / divisor);
}

int Port_Recip32(int divisor)
{
    return (int)((1LL << 32) / divisor);
}

int Port_MenuScale(int baseX, int realX, int realY)
{
    return (int)((long long)baseX * 3 / 4 * 100 * realX / ((long long)baseX * realY));
}

int Port_DoubleToInt64Low(double value)
{
    long long rounded;
    if (value >= -9223372036854775808.0 && value < 9223372036854775808.0)
        rounded = llrint(value);
    else
        rounded = (long long)(1ULL << 63);
    return (int)rounded;
}
