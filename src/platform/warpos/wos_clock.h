/* The game clock on WarpOS: the PPC time base, read without leaving the PPC. */
#ifndef OPENCMR2_WOS_CLOCK_H
#define OPENCMR2_WOS_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Microseconds since the first call; monotonic. */
uint64_t WosClock_Micros(void);
/* The time base rate last measured, in Hz. */
uint32_t WosClock_Rate(void);

#ifdef __cplusplus
}
#endif

#endif
