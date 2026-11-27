/*
 * AHI backend for WarpOS (the facade is platform/amiga/amiga_ahi.h).
 *
 * The 68k backend renders in a 68k Process woken by an AHI sound hook. Here
 * every AmigaOS call is a context switch to the 68k costing milliseconds, so
 * nothing is called per block:
 *
 *  - AHI plays one looped dynamic sample, the ring, on one channel.
 *  - An AHIET_CHANNELINFO effect hook, ten words of 68k machine code, stores
 *    the channel's play offset (in sample frames) into the hook's h_Data
 *    every time AHI mixes.
 *  - The background pump of the platform layer (AmigaSys_SetBackgroundPump,
 *    run from the event poll and from file reads, at most every 50 ms; the
 *    WarpOS build has no network session to service with it) renders blocks
 *    until the writer is kLeadBlocks ahead of the player, pushes the written
 *    cache lines to memory (dcbf) for AHI, and reads the play offset past the
 *    PPC's cached copy.
 *
 * Everything runs on the game's task, so the lock is a depth counter that
 * only keeps the pump from rendering while the game changes voices.
 */
#include "platform/amiga/amiga_ahi.h"
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <powerup/ppcinline/ahi.h>
#include "platform/amiga/amiga_os_end.h"

struct Library *AHIBase;

enum {
    kFrameBytes = 4,      /* interleaved 16-bit stereo */
    kRingBlocks = 32,     /* the looped sample */
    kLeadBlocks = 12,     /* how far the writer keeps ahead of the player */
    kCacheLine = 32
};

/* AHIET_CHANNELINFO hook: h_Data = ((struct AHIEffChannelInfo *)a1)->ahieci_Offset[0]
 *   movem.l a2,-(sp); movea.l a0,a2; move.l 12(a1),d0; move.l d0,16(a2);
 *   moveq #0,d0; movea.l (sp)+,a2; rts
 * (struct Hook: h_Data at 16; AHIEffChannelInfo: ahieci_Offset at 12.) */
static const UWORD kChannelInfoHookCode[] = {
    0x48e7, 0x0020, 0x2448, 0x2029, 0x000c, 0x2540, 0x0010, 0x7000, 0x245f, 0x4e75
};

/* Memory the 68k reads or executes: allocated with AllocVec, owned here. */
struct SharedBlock {
    struct Hook hook;
    struct AHIEffChannelInfo info;
    ULONG infoOffset[1];           /* info's ahieci_Offset[] (declared [0]): one channel */
    UWORD code[sizeof(kChannelInfoHookCode) / sizeof(kChannelInfoHookCode[0])];
};
_Static_assert(offsetof(struct SharedBlock, infoOffset) ==
                   offsetof(struct SharedBlock, info) + sizeof(struct AHIEffChannelInfo),
               "AHI writes the channel's offset right after AHIEffChannelInfo");

static struct MsgPort *s_port;
static struct AHIRequest *s_request;
static int s_deviceOpen;
static struct AHIAudioCtrl *s_control;
static struct SharedBlock *s_shared;
static int s_effectSet;
static int s_soundLoaded;
static int16_t *s_ring;
static uint32_t s_ringFrames;
static uint32_t s_blockFrames;
static uint32_t s_rate;
static uint32_t s_writeFrame;
static AmigaAhiRenderFunc s_render;
static int s_lockDepth;
static unsigned long s_underruns;

static void FlushLines(const void *start, uint32_t bytes)
{
    uintptr_t p = (uintptr_t)start & ~(uintptr_t)(kCacheLine - 1);
    const uintptr_t end = (uintptr_t)start + bytes;
    for (; p < end; p += kCacheLine)
        __asm__ volatile("dcbf 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

/* The hook's h_Data as the 68k last wrote it. dcbf drops the PPC's copy of
 * the line (which holds only this block's own fields, never written here
 * after set-up), so the load below comes from memory. */
static uint32_t PlayFrame(void)
{
    FlushLines(&s_shared->hook.h_Data, sizeof(s_shared->hook.h_Data));
    return (uint32_t)(*(volatile ULONG *)&s_shared->hook.h_Data) % s_ringFrames;
}

static void RenderAt(uint32_t frame, uint32_t frames)
{
    int16_t *out = s_ring + (size_t)frame * 2;
    s_render(out, frames);
    FlushLines(out, frames * kFrameBytes);
}

/* Keeps the writer kLeadBlocks ahead of the player. A writer that fell
 * behind (a long load, a slow frame) restarts one block ahead of it. */
static void Feed(void)
{
    const uint32_t lead = s_blockFrames * kLeadBlocks;
    uint32_t play;
    uint32_t ahead;

    if (s_control == NULL || s_lockDepth != 0)
        return;
    play = PlayFrame();
    ahead = (s_writeFrame + s_ringFrames - play) % s_ringFrames;
    if (ahead > lead + s_blockFrames) {
        ++s_underruns;
        s_writeFrame = (play + s_blockFrames) % s_ringFrames;
        s_writeFrame -= s_writeFrame % s_blockFrames;
        ahead = (s_writeFrame + s_ringFrames - play) % s_ringFrames;
    }
    while (ahead < lead) {
        RenderAt(s_writeFrame, s_blockFrames);
        s_writeFrame = (s_writeFrame + s_blockFrames) % s_ringFrames;
        ahead += s_blockFrames;
    }
}

static void FreeResources(void)
{
    AmigaSys_SetBackgroundPump(NULL);
    if (s_control != NULL) {
        if (s_effectSet) {
            s_shared->info.ahie_Effect = AHIET_CHANNELINFO | AHIET_CANCEL;
            AHI_SetEffect(&s_shared->info, s_control);
            s_effectSet = 0;
        }
        AHI_ControlAudio(s_control, AHIC_Play, FALSE, TAG_DONE);
        if (s_soundLoaded) {
            AHI_UnloadSound(0, s_control);
            s_soundLoaded = 0;
        }
        AHI_FreeAudio(s_control);
        s_control = NULL;
    }
    if (s_deviceOpen) {
        CloseDevice((struct IORequest *)s_request);
        s_deviceOpen = 0;
        AHIBase = NULL;
    }
    if (s_request != NULL) {
        DeleteIORequest((struct IORequest *)s_request);
        s_request = NULL;
    }
    if (s_port != NULL) {
        DeleteMsgPort(s_port);
        s_port = NULL;
    }
    if (s_ring != NULL) {
        FreeVec(s_ring);
        s_ring = NULL;
    }
    if (s_shared != NULL) {
        FreeVec(s_shared);
        s_shared = NULL;
    }
    s_render = NULL;
}

static int OpenDeviceAndMode(uint32_t *rate)
{
    ULONG actual = 0;
    ULONG mode;

    s_port = CreateMsgPort();
    if (s_port == NULL) {
        Sys_Log("audio: CreateMsgPort failed");
        return 0;
    }
    s_request = (struct AHIRequest *)CreateIORequest(s_port, sizeof(struct AHIRequest));
    if (s_request == NULL) {
        Sys_Log("audio: CreateIORequest failed");
        return 0;
    }
    s_request->ahir_Version = 4;
    if (OpenDevice((STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)s_request, 0) != 0) {
        Sys_Log("audio: cannot open ahi.device v4 (%ld)", (long)s_request->ahir_Std.io_Error);
        return 0;
    }
    s_deviceOpen = 1;
    AHIBase = (struct Library *)s_request->ahir_Std.io_Device;
    /* audio.ahi_mode: an AHI audio mode ID (hexadecimal with 0x), for a
     * machine whose AHI default mode cannot be allocated (a driver without
     * its card). 0, the default, is the mode chosen in the AHI prefs. */
    mode = (ULONG)Sys_GetOption("audio.ahi_mode", (int)AHI_DEFAULT_ID);
    s_control = AHI_AllocAudio(AHIA_AudioID, mode,
                               AHIA_MixFreq, *rate,
                               AHIA_Channels, 1,
                               AHIA_Sounds, 1,
                               TAG_DONE);
    if (s_control == NULL) {
        Sys_Log("audio: AHI_AllocAudio failed for mode %08lx (0 = the AHI default) at %lu Hz; "
                "set audio.ahi_mode to a working mode",
                (unsigned long)mode, (unsigned long)*rate);
        return 0;
    }
    AHI_ControlAudio(s_control, AHIC_MixFreq_Query, (ULONG)&actual, TAG_DONE);
    if (actual != 0)
        *rate = actual;
    return 1;
}

static void CloseAtExit(void)
{
    AmigaAhi_Close();
}

int AmigaAhi_Open(uint32_t *rate, uint32_t blockMs, uint32_t maxBlockFrames, AmigaAhiRenderFunc render)
{
    static int s_exitRegistered;
    struct AHISampleInfo sample;
    uint32_t i;

    if (s_control != NULL)
        return 1;
    if (!OpenDeviceAndMode(rate)) {
        FreeResources();
        return 0;
    }
    s_blockFrames = *rate * blockMs / 1000;
    if (s_blockFrames > maxBlockFrames)
        s_blockFrames = maxBlockFrames;
    if (s_blockFrames == 0) {
        Sys_Log("audio: no block size for %lu ms at %lu Hz", (unsigned long)blockMs, (unsigned long)*rate);
        FreeResources();
        return 0;
    }
    s_rate = *rate;
    s_ringFrames = s_blockFrames * kRingBlocks;
    s_render = render;
    s_underruns = 0;
    s_ring = (int16_t *)AllocVec(s_ringFrames * kFrameBytes, MEMF_PUBLIC | MEMF_CLEAR);
    s_shared = (struct SharedBlock *)AllocVec(sizeof(*s_shared), MEMF_PUBLIC | MEMF_CLEAR);
    if (s_ring == NULL || s_shared == NULL) {
        Sys_Log("audio: cannot allocate the %lu-frame ring", (unsigned long)s_ringFrames);
        FreeResources();
        return 0;
    }
    FlushLines(s_ring, s_ringFrames * kFrameBytes);

    /* The hook's code is copied into this block, pushed to memory and made
     * visible to the 68k's instruction cache before AHI can call it. */
    memcpy(s_shared->code, kChannelInfoHookCode, sizeof(kChannelInfoHookCode));
    s_shared->hook.h_Entry = (HOOKFUNC)s_shared->code;
    s_shared->hook.h_Data = 0;
    s_shared->info.ahie_Effect = AHIET_CHANNELINFO;
    s_shared->info.ahieci_Func = &s_shared->hook;
    s_shared->info.ahieci_Channels = 1;
    FlushLines(s_shared, sizeof(*s_shared));
    CacheClearU();

    sample.ahisi_Type = AHIST_S16S;
    sample.ahisi_Address = s_ring;
    sample.ahisi_Length = s_ringFrames;
    if (AHI_LoadSound(0, AHIST_DYNAMICSAMPLE, &sample, s_control) != AHIE_OK) {
        Sys_Log("audio: AHI_LoadSound refused the ring");
        FreeResources();
        return 0;
    }
    s_soundLoaded = 1;

    /* Lead blocks rendered before playback starts at frame 0. */
    s_writeFrame = 0;
    for (i = 0; i < kLeadBlocks; ++i) {
        RenderAt(s_writeFrame, s_blockFrames);
        s_writeFrame += s_blockFrames;
    }

    if (AHI_SetEffect(&s_shared->info, s_control) != AHIE_OK) {
        Sys_Log("audio: AHI refused the channel-info effect");
        FreeResources();
        return 0;
    }
    s_effectSet = 1;
    AHI_Play(s_control,
             AHIP_BeginChannel, 0,
             AHIP_Freq, *rate,
             AHIP_Vol, 0x10000,
             AHIP_Pan, 0x8000,
             AHIP_Sound, 0,
             AHIP_EndChannel, 0,
             TAG_DONE);
    if (AHI_ControlAudio(s_control, AHIC_Play, TRUE, TAG_DONE) != AHIE_OK) {
        Sys_Log("audio: AHI refused to start playback");
        FreeResources();
        return 0;
    }
    AmigaSys_SetBackgroundPump(Feed);
    if (!s_exitRegistered && atexit(CloseAtExit) == 0)
        s_exitRegistered = 1;
    Sys_Log("audio: AHI ring %lu frames at %p, %lu Hz, %lu-frame blocks, %d blocks ahead",
            (unsigned long)s_ringFrames, (void *)s_ring, (unsigned long)s_rate, (unsigned long)s_blockFrames,
            (int)kLeadBlocks);
    return 1;
}

void AmigaAhi_Close(void)
{
    if (s_control != NULL && s_underruns != 0)
        Sys_Log("audio: %lu ring underruns", s_underruns);
    FreeResources();
}

uint32_t AmigaAhi_GetLatencyMs(void)
{
    if (s_control == NULL || s_rate == 0)
        return 0;
    return s_blockFrames * kLeadBlocks * 1000 / s_rate;
}

void AmigaAhi_Lock(void)
{
    ++s_lockDepth;
}

void AmigaAhi_Unlock(void)
{
    if (s_lockDepth > 0)
        --s_lockDepth;
}
