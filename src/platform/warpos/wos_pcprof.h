/* A sampling profiler for the WarpOS build (MPC7410 performance monitor). */
#ifndef OPENCMR2_WOS_PCPROF_H
#define OPENCMR2_WOS_PCPROF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Called once a frame. Starts sampling when ENV:OPENCMR2_PCPROF is set (its
 * presence is checked by the caller), and writes PROGDIR:pcsamp.bin when the
 * buffer is full. */
void WosPcProf_Start(void);
void WosPcProf_Poll(void);

#ifdef __cplusplus
}
#endif

#endif
