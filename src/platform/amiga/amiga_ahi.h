/*
 * Plain-C facade over ahi.device. Its implementation needs the NDK types, whose
 * BYTE clashes with port/types.h, so nothing here names either.
 *
 * Audio is rendered by a dedicated Process. AHI's sound hook paces it: each
 * block that starts playing queues the following one and wakes the Process, so
 * mixing happens exactly when a block is free and never by polling a timer.
 */
#ifndef OPENCMR2_AMIGA_AHI_H
#define OPENCMR2_AMIGA_AHI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fills a block of native interleaved 16-bit stereo; called holding the lock. */
typedef void (*AmigaAhiRenderFunc)(int16_t *output, uint32_t frames);

/* rate is the requested rate; the mode's actual rate comes back through it and
   is what render must produce. A mode may run at a fixed rate far from the
   request, so blocks are sized in time, from that actual rate, and never pass
   maxBlockFrames. Returns 0, with the reason logged, on failure. */
int AmigaAhi_Open(uint32_t *rate, uint32_t blockMs, uint32_t maxBlockFrames, AmigaAhiRenderFunc render);
void AmigaAhi_Close(void);

/* Time from rendering a frame to hearing it, as far as this ring decides it:
   the renderer keeps up to AHI_RENDERER_BLOCKS blocks ready behind the one AHI
   has queued, so a rendered frame waits that many blocks plus the queued one.
   AHI's own mixing buffer comes on top and is not known here. 0 when closed. */
uint32_t AmigaAhi_GetLatencyMs(void);

/* Serialises game-side voice changes with rendering. Valid before Open. */
void AmigaAhi_Lock(void);
void AmigaAhi_Unlock(void);

#ifdef __cplusplus
}
#endif

#endif
