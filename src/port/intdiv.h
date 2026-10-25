/*
 * 64-bit-intermediate integer helpers.
 *
 * GCC 6.5 for m68k hits an internal compiler error on 64-bit division in a
 * translation unit built with -malign-int (Domain A). These helpers keep the
 * 64-bit expression in Domain B, which is built without -malign-int. The
 * interface is 32-bit ints, so the cross-domain ABI does not depend on 64-bit
 * argument alignment.
 *
 * The divisor is the caller's responsibility, exactly as in the original
 * expressions; there is no zero check.
 */
#ifndef OPENCMR2_PORT_INTDIV_H
#define OPENCMR2_PORT_INTDIV_H

#ifdef __cplusplus
extern "C" {
#endif

/* (int)(((long long)a * b) / divisor) */
int Port_MulDiv(int a, int b, int divisor);
/* (int)(((long long)a * b + add) / divisor) */
int Port_MulAddDiv(int a, int b, int add, int divisor);
/* (int)((1LL << 32) / divisor) */
int Port_Recip32(int divisor);
/* Sprite.cpp's menu-largest-scale expression, kept bit-exact. */
int Port_MenuScale(int baseX, int realX, int realY);
/* Low 32 bits of the QIFIST 64-bit FISTP rounding; the callers only keep
 * the int result. */
int Port_DoubleToInt64Low(double value);

#ifdef __cplusplus
}
#endif

#endif
