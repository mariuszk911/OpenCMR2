/* ahi.device output: see amiga_ahi.h. */
#include "platform/amiga/amiga_ahi.h"

#include <stdlib.h>

#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <devices/ahi.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/hooks.h>
#include <clib/alib_protos.h>
#include <proto/ahi.h>
#include <proto/dos.h>
#include <proto/exec.h>

void Sys_Log(const char *format, ...);

/*
 * Eight blocks in a ring: one playing, one queued in AHI, the rest for the
 * renderer. AHI asks for blocks in bursts, one mix pass at a time, and the
 * Process only runs after the pass: the Emu68 HDMI mode's pass was measured at
 * about 92 ms, so the ready audio has to outlast that or every pass runs dry.
 * The hook only reads the ready count, so a late renderer costs one silent
 * block. The old block is never replayed: repeating it is the audible crunch.
 */
#define AHI_RING_BLOCKS 8
#define AHI_RENDERER_BLOCKS (AHI_RING_BLOCKS - 2)
#define AHI_RING_SOUND 0
#define AHI_SILENCE_SOUND 1
#define AHI_FRAME_BYTES 4
#define AHI_UNITY 0x10000
#define AHI_PAN_CENTRE 0x8000
#define AHI_MIXER_STACK 16384
/* Over the game and any idle work, under the file systems and input.device. */
#define AHI_MIXER_PRIORITY 5

struct Library *AHIBase;

static struct SignalSemaphore s_lock;
static int s_lockReady;
static struct MsgPort *s_port;
static struct AHIRequest *s_request;
static struct AHIAudioCtrl *s_control;
static struct Hook s_soundHook;
static struct Task *s_parent;
static struct Task *volatile s_mixer;
static AmigaAhiRenderFunc s_render;
static int16_t *s_ring;
static int16_t *s_silence;
static uint32_t s_blockFrames;
static uint32_t s_rate;
static BYTE s_wakeBit = -1;
static volatile ULONG s_wakeMask;
static volatile uint32_t s_queued;   /* blocks handed to AHI: hook only */
static volatile uint32_t s_rendered; /* blocks written: renderer only */
static volatile uint32_t s_underruns;
static int s_exitRegistered;

/* AHI calls this as a block starts, possibly from an interrupt. AHI_SetSound
   without AHISF_IMM queues the next block behind the one now starting. */
static ULONG SoundFunc(register struct Hook *hook __asm("a0"),
                       register struct AHIAudioCtrl *control __asm("a2"),
                       register struct AHISoundMessage *message __asm("a1"))
{
    (void)hook;
    (void)message;
    if (s_queued != s_rendered) {
        AHI_SetSound(0, AHI_RING_SOUND, (s_queued % AHI_RING_BLOCKS) * s_blockFrames,
                     s_blockFrames, control, 0);
        ++s_queued;
    } else {
        AHI_SetSound(0, AHI_SILENCE_SOUND, 0, s_blockFrames, control, 0);
        ++s_underruns;
    }
    Signal(s_mixer, s_wakeMask);
    return 0;
}

static void RenderNext(void)
{
    int16_t *block = s_ring + (s_rendered % AHI_RING_BLOCKS) * s_blockFrames * 2;
    ObtainSemaphore(&s_lock);
    s_render(block, s_blockFrames);
    ReleaseSemaphore(&s_lock);
    ++s_rendered;
}

/* Stays off dos.library and libnix, and never touches the FPU. */
static void MixerEntry(void)
{
    const BYTE bit = AllocSignal(-1);

    s_wakeBit = bit;
    s_wakeMask = bit >= 0 ? 1ul << bit : 0;
    s_mixer = FindTask(NULL);
    Signal(s_parent, SIGF_SINGLE);
    if (bit < 0)
        return;

    for (;;) {
        const ULONG got = Wait(s_wakeMask | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C)
            break;
        while (s_rendered - s_queued < AHI_RENDERER_BLOCKS)
            RenderNext();
    }

    FreeSignal(bit);
    /* Forbid holds until this Process is gone, so Close can free the code. */
    Forbid();
    s_mixer = NULL;
    Signal(s_parent, SIGF_SINGLE);
}

static void WaitForMixer(void)
{
    Wait(SIGF_SINGLE);
}

static int StartMixer(void)
{
    s_parent = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    if (CreateNewProcTags(NP_Entry, (ULONG)MixerEntry,
                          NP_Name, (ULONG)"OpenCMR2 audio",
                          NP_StackSize, AHI_MIXER_STACK,
                          NP_Priority, s_parent->tc_Node.ln_Pri + AHI_MIXER_PRIORITY,
                          TAG_DONE) == NULL) {
        Sys_Log("audio: cannot create the mixer process (%ld)", (long)IoErr());
        return 0;
    }
    WaitForMixer();
    if (s_wakeBit < 0) {
        s_mixer = NULL;
        Sys_Log("audio: no free signal for the mixer process");
        return 0;
    }
    return 1;
}

static void StopMixer(void)
{
    if (s_mixer == NULL)
        return;
    SetSignal(0, SIGF_SINGLE);
    Signal(s_mixer, SIGBREAKF_CTRL_C);
    WaitForMixer();
}

static int LoadBlocks(void)
{
    struct AHISampleInfo info;

    info.ahisi_Type = AHIST_S16S;
    info.ahisi_Address = s_ring;
    info.ahisi_Length = s_blockFrames * AHI_RING_BLOCKS;
    if (AHI_LoadSound(AHI_RING_SOUND, AHIST_DYNAMICSAMPLE, &info, s_control) != AHIE_OK)
        return 0;
    info.ahisi_Address = s_silence;
    info.ahisi_Length = s_blockFrames;
    return AHI_LoadSound(AHI_SILENCE_SOUND, AHIST_SAMPLE, &info, s_control) == AHIE_OK;
}

static void FreeResources(void)
{
    if (s_control != NULL) {
        AHI_FreeAudio(s_control);
        s_control = NULL;
    }
    if (s_ring != NULL) {
        FreeVec(s_ring);
        s_ring = NULL;
    }
    if (s_silence != NULL) {
        FreeVec(s_silence);
        s_silence = NULL;
    }
    if (AHIBase != NULL) {
        CloseDevice((struct IORequest *)s_request);
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
}

static int OpenDeviceAndMode(uint32_t *rate)
{
    ULONG actual = 0;

    s_port = CreateMsgPort();
    if (s_port == NULL)
        return 0;
    s_request = (struct AHIRequest *)CreateIORequest(s_port, sizeof(struct AHIRequest));
    if (s_request == NULL)
        return 0;
    s_request->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)s_request, 0) != 0) {
        Sys_Log("audio: cannot open ahi.device v4 (%ld)", (long)s_request->ahir_Std.io_Error);
        return 0;
    }
    AHIBase = (struct Library *)s_request->ahir_Std.io_Device;

    /* One channel: AHI scales its output by the channel count, so a single
       stereo stream keeps full level with no pre-gain to clip. */
    s_soundHook.h_Entry = (HOOKFUNC)SoundFunc;
    s_control = AHI_AllocAudio(AHIA_AudioID, AHI_DEFAULT_ID,
                               AHIA_MixFreq, *rate,
                               AHIA_Channels, 1,
                               AHIA_Sounds, 2,
                               AHIA_SoundFunc, (ULONG)&s_soundHook,
                               TAG_DONE);
    if (s_control == NULL) {
        Sys_Log("audio: AHI_AllocAudio failed for the default mode at %lu Hz", (unsigned long)*rate);
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
    uint32_t blockFrames;
    uint32_t i;

    if (s_control != NULL)
        return 1;
    AmigaAhi_Lock();
    AmigaAhi_Unlock();
    if (!OpenDeviceAndMode(rate)) {
        FreeResources();
        return 0;
    }

    blockFrames = *rate * blockMs / 1000;
    blockFrames = blockFrames > maxBlockFrames ? maxBlockFrames : blockFrames;
    s_render = render;
    s_blockFrames = blockFrames;
    s_rate = *rate;
    s_queued = 1;   /* block 0 goes straight to AHI_Play below */
    s_rendered = 0;
    s_underruns = 0;
    s_ring = (int16_t *)AllocVec(blockFrames * AHI_RING_BLOCKS * AHI_FRAME_BYTES, MEMF_PUBLIC | MEMF_CLEAR);
    s_silence = (int16_t *)AllocVec(blockFrames * AHI_FRAME_BYTES, MEMF_PUBLIC | MEMF_CLEAR);
    if (s_ring == NULL || s_silence == NULL || !LoadBlocks() || !StartMixer()) {
        Sys_Log("audio: cannot prepare the output blocks");
        FreeResources();
        return 0;
    }

    /* The first hook call consumes block 1, so it has to exist already. */
    for (i = 0; i < AHI_RENDERER_BLOCKS + 1; ++i)
        RenderNext();
    AHI_ControlAudio(s_control, AHIC_OutputVolume, AHI_UNITY, TAG_DONE);
    AHI_Play(s_control,
             AHIP_BeginChannel, 0,
             AHIP_Freq, *rate,
             AHIP_Vol, AHI_UNITY,
             AHIP_Pan, AHI_PAN_CENTRE,
             AHIP_Sound, AHI_RING_SOUND,
             AHIP_Offset, 0,
             AHIP_Length, blockFrames,
             AHIP_EndChannel, 0,
             TAG_DONE);
    if (AHI_ControlAudio(s_control, AHIC_Play, TRUE, TAG_DONE) != AHIE_OK) {
        Sys_Log("audio: AHI refused to start playback");
        StopMixer();
        FreeResources();
        return 0;
    }
    if (!s_exitRegistered && atexit(CloseAtExit) == 0)
        s_exitRegistered = 1;
    Sys_Log("audio: AHI %lu Hz, %lu frames per block", (unsigned long)*rate, (unsigned long)blockFrames);
    return 1;
}

void AmigaAhi_Close(void)
{
    if (s_control == NULL)
        return;
    /* Stopping first ends the hook, so the Process gets no signal once gone. */
    AHI_ControlAudio(s_control, AHIC_Play, FALSE, TAG_DONE);
    StopMixer();
    Sys_Log("audio: closed after %lu blocks, %lu late", (unsigned long)s_rendered, (unsigned long)s_underruns);
    FreeResources();
}

uint32_t AmigaAhi_GetLatencyMs(void)
{
    if (s_control == NULL || s_rate == 0)
        return 0;
    return (uint32_t)((AHI_RENDERER_BLOCKS + 1) * s_blockFrames * 1000u / s_rate);
}

void AmigaAhi_Lock(void)
{
    if (!s_lockReady) {
        InitSemaphore(&s_lock);
        s_lockReady = 1;
    }
    ObtainSemaphore(&s_lock);
}

void AmigaAhi_Unlock(void)
{
    ReleaseSemaphore(&s_lock);
}
