/*
 * Workarounds for the AmigaOS 68k C library (libnix / bebbo's amiga-gcc).
 *
 * This file must be compiled with -fno-builtin -fno-math-errno (see
 * src/amiga.cmake): without -fno-builtin gcc turns "(float)sin(x)" straight
 * back into a call to sinf, i.e. into infinite recursion; without
 * -fno-math-errno gcc expands __builtin_sqrt into fsqrt plus a fallback call
 * to the library sqrt for a negative argument, which is this same function.
 *
 * Float math. The toolchain's generic libm float functions (sqrtf, cosf,
 * sinf, atan2f, fmodf, ...) are soft-float code that returns the result in
 * d0, while every -mhard-float caller reads the result from fp0 and gets
 * whatever was left there. libnix only ships the double FPU functions
 * (libm881), so the *f calls fall through to the broken library. Symptoms
 * are garbage/stale values in anything that normalises or rotates: the 3D
 * view rotated by 90 degrees on the RTCW port, and here the entity/frame
 * matrices (librw Matrix::rotate) went to 1.59e15.
 *
 * Each float function is routed through the double version, which returns in
 * fp0. sqrt/sqrtf are single FPU instructions (the build disables the
 * errno fallback that would call sqrt again). Only *f whose backing double is
 * hard-float (libm881) is provided; omitting one is deliberate -- routing it
 * through a soft lib__m__ double is the same bug one level down (see
 * round/copysign).
 */
#include <math.h>
#include <string.h>
#include <stdint.h>

/* log2/log2f are macros in this libnix <math.h>. */
#undef log2
#undef log2f

/* ---- basic ------------------------------------------------------------- */
/* The fixed-point tables call sqrt() (double); the generic libm sqrt on this
 * toolchain has the same soft-float ABI problem as the *f functions and
 * returns 0 to a hard-float caller. Route it through the FPU builtin. */
double sqrt(double x)          { return __builtin_sqrt(x); }
float sqrtf(float x)           { return (float)__builtin_sqrt((double)x); }
float sinf(float x)            { return (float)sin(x); }
float cosf(float x)            { return (float)cos(x); }
float tanf(float x)            { return (float)tan(x); }
float asinf(float x)           { return (float)asin(x); }
float acosf(float x)           { return (float)acos(x); }
float atanf(float x)           { return (float)atan(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float floorf(float x)          { return (float)floor(x); }
float ceilf(float x)           { return (float)ceil(x); }
float rintf(float x)           { return (float)rint(x); }
float fmodf(float x, float y)  { return (float)fmod(x, y); }
float powf(float x, float y)   { return (float)pow(x, y); }
float expf(float x)            { return (float)exp(x); }
float logf(float x)            { return (float)log(x); }
float log10f(float x)          { return (float)log10(x); }
float log2f(float x)           { return (float)log2((double)x); }
double log2(double x)          { return log(x) / 0.69314718055994530942; }

/* ---- rounding / sign --------------------------------------------------- */
/* libm881 lacks round() and copysign(), so those fall back to lib__m__ (soft
 * ABI: result in d0, hard-float callers read fp0). Define them here. */
double round(double x)
{
	return trunc(x + copysign(0.5, x));
}
double copysign(double x, double y)
{
	unsigned int ux[2], uy[2];
	memcpy(ux, &x, sizeof(ux));
	memcpy(uy, &y, sizeof(uy));
	ux[0] = (ux[0] & 0x7fffffffu) | (uy[0] & 0x80000000u);
	memcpy(&x, ux, sizeof(x));
	return x;
}
float roundf(float x)          { return (float)round(x); }
float truncf(float x)          { return (float)trunc(x); }
float copysignf(float x, float y) { return (float)copysign(x, y); }

/* fabsf must clear the sign bit: the naive `x < 0 ? -x : x` returns -0.0 for
 * -0.0 and leaves a NaN's sign set. Bit-clear instead (no libm call, exact). */
float fabsf(float x)
{
	unsigned int u;
	memcpy(&u, &x, sizeof(u));
	u &= 0x7fffffffu;
	memcpy(&x, &u, sizeof(x));
	return x;
}

/* ---- hyperbolic -------------------------------------------------------- */
float sinhf(float x)           { return (float)sinh(x); }
float coshf(float x)           { return (float)cosh(x); }
float tanhf(float x)           { return (float)tanh(x); }

/* ---- power / root / misc ---------------------------------------------- */
float ldexpf(float x, int e)   { return (float)ldexp(x, e); }

/* fmaxf/fminf: plain FPU compares, no libm (C99 NaN rules: a quiet NaN loses). */
float fmaxf(float x, float y)  { return x > y ? x : (x != x ? y : (y != y ? x : y)); }
float fminf(float x, float y)  { return x < y ? x : (x != x ? y : (y != y ? x : y)); }

/* The CMR2 X87Single fixed-point tables use these double/float helpers too.
 * The generic libm versions have the same soft-float ABI problem, so provide
 * hard-float implementations with no libm dependency. */
double fabs(double x)
{
	uint64_t u;
	memcpy(&u, &x, sizeof(u));
	u &= UINT64_C(0x7fffffffffffffff);
	memcpy(&x, &u, sizeof(x));
	return x;
}

double fmax(double x, double y) { return x > y ? x : (x != x ? y : (y != y ? x : y)); }
double fmin(double x, double y) { return x < y ? x : (x != x ? y : (y != y ? x : y)); }

/* FTOI(__int64, ...) in the CMR2_QIFIST units is llrint. libnix's version is a
 * soft-float stub and does not return in the hard-float calling convention, so
 * it produced 0 for every conversion. Round half away from zero, which is exact
 * for the integer-valued fixed-point inputs the game converts. */
long long llrint(double x) { return (long long)(x < 0.0 ? x - 0.5 : x + 0.5); }
long lrint(double x) { return (long)(x < 0.0 ? x - 0.5 : x + 0.5); }
long long llrintf(float x) { return llrint((double)x); }
long lrintf(float x) { return lrint((double)x); }

/* frexp/ldexp are called by the X87 single-precision rounding in
 * game/X87Single.h for every fixed-point operation, so they must be O(1) in
 * the normal range rather than scale bit by bit. */
double frexp(double x, int *e)
{
	uint64_t u;
	int exponent;

	memcpy(&u, &x, sizeof(u));
	exponent = (int)((u >> 52) & 0x7ff);
	if (exponent == 0) {
		if (x == 0.0) {
			*e = 0;
			return x;
		}
		/* Subnormal: scale exactly into the normal range (2^54). */
		x *= 18014398509481984.0;
		memcpy(&u, &x, sizeof(u));
		exponent = (int)((u >> 52) & 0x7ff) - 54;
	} else if (exponent == 0x7ff) {
		*e = 0;
		return x;
	}
	*e = exponent - 1022;
	u = (u & UINT64_C(0x800fffffffffffff)) | UINT64_C(0x3fe0000000000000);
	memcpy(&x, &u, sizeof(x));
	return x;
}

double ldexp(double x, int e)
{
	uint64_t u, m, g, rem, half;
	int exponent, target, shift;

	if (x == 0.0 || !isfinite(x))
		return x;
	if (e > 2100)
		e = 2100;
	if (e < -2100)
		e = -2100;
	memcpy(&u, &x, sizeof(u));
	exponent = (int)((u >> 52) & 0x7ff);
	if (exponent == 0) {
		/* Subnormal operand: normalise exactly (2^54) and fold the scale. */
		x *= 18014398509481984.0;
		memcpy(&u, &x, sizeof(u));
		exponent = (int)((u >> 52) & 0x7ff);
		e -= 54;
	}
	target = exponent + e;
	if (target >= 0x7ff) {
		/* The exact result is at least 2^1024, so it rounds to infinity. */
		u = (u & UINT64_C(0x8000000000000000)) | UINT64_C(0x7ff0000000000000);
		memcpy(&x, &u, sizeof(x));
		return x;
	}
	if (target >= 1) {
		u = (u & UINT64_C(0x800fffffffffffff)) | ((uint64_t)(unsigned int)target << 52);
		memcpy(&x, &u, sizeof(x));
		return x;
	}
	/* Subnormal or zero result: round M * 2^(target-1) to the nearest
	 * multiple of 2^-1074, ties to even, instead of scaling bit by bit. */
	m = (u & UINT64_C(0x000fffffffffffff)) | UINT64_C(0x0010000000000000);
	shift = 1 - target;
	if (shift >= 54) {
		g = 0;
	} else {
		g = m >> shift;
		rem = m & ((UINT64_C(1) << shift) - 1);
		half = UINT64_C(1) << (shift - 1);
		if (rem > half || (rem == half && (g & 1) != 0))
			g++;
	}
	u = (u & UINT64_C(0x8000000000000000)) | g;
	memcpy(&x, &u, sizeof(x));
	return x;
}

float nextafterf(float x, float y)
{
	uint32_t ux;
	if (x == y)
		return y;
	if (x == 0.0f) {
		/* Either zero steps to the smallest subnormal with y's sign. */
		ux = y < 0.0f ? 0x80000001u : 0x00000001u;
		memcpy(&x, &ux, sizeof(x));
		return x;
	}
	memcpy(&ux, &x, sizeof(ux));
	ux += ((y > x) == (x > 0.0f)) ? 1u : (uint32_t)-1;
	memcpy(&x, &ux, sizeof(x));
	return x;
}

/* modff/frexpf take an out-parameter. */
float modff(float x, float *ip)
{
	double t = trunc((double)x);
	*ip = (float)t;
	return (float)((double)x - t);
}

float frexpf(float x, int *e)
{
	int de;
	double dm = frexp((double)x, &de);
	*e = de;
	return (float)dm;
}
