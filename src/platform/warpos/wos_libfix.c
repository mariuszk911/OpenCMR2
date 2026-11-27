/*
 * libm replacements for the WarpOS build: frexp and ldexp on the IEEE bits
 * (see wos_fastmath.h for why and for the exactness argument). Linked as
 * objects, they come before libm.a, so newlib's members are not pulled in
 * for these two names.
 */
#include "platform/warpos/wos_fastmath.h"

double frexp(double x, int *exponent)
{
    return WosMath_Frexp(x, exponent);
}

double ldexp(double x, int exponent)
{
    return WosMath_Ldexp(x, exponent);
}
