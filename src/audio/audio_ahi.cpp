// Audio on AmigaOS: see port/audio.h. The game's buffers become voices of the
// fixed-point mixer, and platform/amiga/amiga_ahi.c plays the mixed stream
// through one AHI channel. Sample data stays little-endian; mixer.cpp converts
// it as it reads.
//
// Movies are stubbed on this target (video/movie_stub.cpp), so nothing queues
// stream audio; the stream calls only keep the interface whole.
#include "audio/mixer.h"
#include "platform/amiga/amiga_ahi.h"
#include "port/audio.h"
#include "port/sys.h"

#include <stdlib.h>
#include <string.h>

namespace {

struct AudioData {
    BYTE *bytes;
    DWORD size;
    MixSample sample;
    DWORD sampleRate;
    int refs;
};

} // namespace

struct AudioBuffer {
    AudioData *data;
    MixVoice voice;
};

struct AudioStream {
    int unused;
};

namespace {

enum {
    DEFAULT_RATE = 22050,
    DEFAULT_BLOCK_MS = 20,
    MIN_BLOCK_MS = 10,
    MAX_BLOCK_MS = 40
};

Mixer s_mixer;
bool s_mixerReady;

void Render(int16_t *output, uint32_t frames)
{
    Mixer_Render(&s_mixer, output, frames);
}

void EnsureMixer(void)
{
    if (s_mixerReady)
        return;
    Mixer_Init(&s_mixer, DEFAULT_RATE);
    s_mixerReady = true;
}

struct Lock {
    Lock() { AmigaAhi_Lock(); }
    ~Lock() { AmigaAhi_Unlock(); }
};

DWORD FrameBytes(const AudioData *data)
{
    return (DWORD)(data->sample.bits / 8 * data->sample.channels);
}

int ClampOption(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

} // namespace

extern "C" BOOL Audio_Init(int sampleRate, int channels)
{
    (void)channels; // the output is stereo; a mono AHI mode folds it down
    EnsureMixer();
    uint32_t rate = (uint32_t)Sys_GetOption("audio.rate", sampleRate > 0 ? sampleRate : DEFAULT_RATE);
    const int blockMs = ClampOption(Sys_GetOption("audio.block_ms", DEFAULT_BLOCK_MS), MIN_BLOCK_MS, MAX_BLOCK_MS);

    // Blocks render as soon as the output opens, before the rate is known here;
    // the rate is correct by the time any voice can be started, as this lock
    // is held throughout.
    Lock lock;
    if (!AmigaAhi_Open(&rate, (uint32_t)blockMs, MIXER_MAX_BLOCK_FRAMES, Render))
        return FALSE;
    s_mixer.rate = rate;
    return TRUE;
}

extern "C" void Audio_Shutdown(void)
{
    AmigaAhi_Close();
    // Buffers the game did not release keep their data until exit.
}

extern "C" void Audio_SetPaused(BOOL paused)
{
    EnsureMixer();
    Lock lock;
    s_mixer.paused = paused != FALSE;
}

extern "C" AudioBuffer *Audio_CreateBuffer(DWORD sampleRate, int bits, int channels, DWORD bytes)
{
    if ((bits != 8 && bits != 16) || channels < 1 || channels > 2 || bytes == 0)
        return NULL;
    EnsureMixer();
    AudioData *data = (AudioData *)calloc(1, sizeof(AudioData));
    AudioBuffer *buffer = (AudioBuffer *)calloc(1, sizeof(AudioBuffer));
    BYTE *pcm = (BYTE *)malloc(bytes);
    if (data == NULL || buffer == NULL || pcm == NULL) {
        free(data);
        free(buffer);
        free(pcm);
        return NULL;
    }
    memset(pcm, bits == 8 ? 0x80 : 0, bytes);
    data->bytes = pcm;
    data->size = bytes;
    data->sampleRate = sampleRate;
    data->refs = 1;
    data->sample.bytes = pcm;
    data->sample.bits = bits;
    data->sample.channels = channels;
    data->sample.frames = bytes / (DWORD)(bits / 8 * channels);
    buffer->data = data;
    MixVoice_Init(&buffer->voice, &data->sample, sampleRate);
    return buffer;
}

extern "C" AudioBuffer *Audio_DuplicateBuffer(AudioBuffer *source)
{
    if (source == NULL)
        return NULL;
    AudioBuffer *buffer = (AudioBuffer *)calloc(1, sizeof(AudioBuffer));
    if (buffer == NULL)
        return NULL;
    Lock lock;
    buffer->data = source->data;
    ++buffer->data->refs;
    MixVoice_Init(&buffer->voice, &buffer->data->sample, source->voice.frequency);
    buffer->voice.volume = source->voice.volume;
    buffer->voice.pan = source->voice.pan;
    return buffer;
}

extern "C" void Audio_ReleaseBuffer(AudioBuffer *buffer)
{
    if (buffer == NULL)
        return;
    AudioData *orphan = NULL;
    {
        Lock lock;
        Mixer_Stop(&s_mixer, &buffer->voice);
        if (--buffer->data->refs == 0)
            orphan = buffer->data;
    }
    if (orphan != NULL) {
        free(orphan->bytes);
        free(orphan);
    }
    free(buffer);
}

extern "C" DWORD Audio_GetBufferSize(AudioBuffer *buffer)
{
    return buffer != NULL ? buffer->data->size : 0;
}

extern "C" void Audio_GetBufferFormat(AudioBuffer *buffer, DWORD *sampleRate, int *bits, int *channels)
{
    if (buffer == NULL)
        return;
    if (sampleRate != NULL)
        *sampleRate = buffer->data->sampleRate;
    if (bits != NULL)
        *bits = buffer->data->sample.bits;
    if (channels != NULL)
        *channels = buffer->data->sample.channels;
}

// No lock is held across Lock/Unlock: the game only writes the region behind
// the play cursor, as it did with DirectSound, and decoding a music block
// inside the mutual exclusion would stall the mixer.
extern "C" BOOL Audio_LockBuffer(AudioBuffer *buffer, DWORD offset, DWORD bytes, void **p1, DWORD *n1,
                                 void **p2, DWORD *n2)
{
    if (buffer == NULL || p1 == NULL || n1 == NULL || p2 == NULL || n2 == NULL)
        return FALSE;
    const DWORD size = buffer->data->size;
    offset %= size;
    const DWORD first = bytes < size - offset ? bytes : size - offset;
    const DWORD rest = bytes - first;
    *p1 = buffer->data->bytes + offset;
    *n1 = first;
    *p2 = rest != 0 ? buffer->data->bytes : NULL;
    *n2 = rest < size ? rest : size;
    return TRUE;
}

extern "C" void Audio_UnlockBuffer(AudioBuffer *buffer)
{
    (void)buffer;
}

extern "C" void Audio_Play(AudioBuffer *buffer, BOOL loop)
{
    if (buffer == NULL)
        return;
    Lock lock;
    Mixer_Play(&s_mixer, &buffer->voice, loop != FALSE);
}

extern "C" void Audio_Stop(AudioBuffer *buffer)
{
    if (buffer == NULL)
        return;
    Lock lock;
    Mixer_Stop(&s_mixer, &buffer->voice);
}

extern "C" BOOL Audio_IsPlaying(AudioBuffer *buffer)
{
    if (buffer == NULL)
        return FALSE;
    Lock lock;
    return buffer->voice.playing ? TRUE : FALSE;
}

extern "C" DWORD Audio_GetPlayPosition(AudioBuffer *buffer)
{
    if (buffer == NULL)
        return 0;
    Lock lock;
    return buffer->voice.frame * FrameBytes(buffer->data);
}

extern "C" void Audio_SetPlayPosition(AudioBuffer *buffer, DWORD position)
{
    if (buffer == NULL)
        return;
    Lock lock;
    Mixer_SetPosition(&buffer->voice, position / FrameBytes(buffer->data));
}

extern "C" void Audio_SetVolume(AudioBuffer *buffer, LONG volume)
{
    if (buffer == NULL)
        return;
    volume = volume < AUDIO_VOLUME_MIN ? AUDIO_VOLUME_MIN : (volume > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : volume);
    Lock lock;
    Mixer_SetVolumePan(&s_mixer, &buffer->voice, volume, buffer->voice.pan);
}

extern "C" void Audio_SetPan(AudioBuffer *buffer, LONG pan)
{
    if (buffer == NULL)
        return;
    pan = pan < AUDIO_PAN_LEFT ? AUDIO_PAN_LEFT : (pan > AUDIO_PAN_RIGHT ? AUDIO_PAN_RIGHT : pan);
    Lock lock;
    Mixer_SetVolumePan(&s_mixer, &buffer->voice, buffer->voice.volume, pan);
}

extern "C" void Audio_SetFrequency(AudioBuffer *buffer, DWORD frequency)
{
    if (buffer == NULL)
        return;
    Lock lock;
    Mixer_SetFrequency(&s_mixer, &buffer->voice, frequency != 0 ? frequency : buffer->data->sampleRate);
}

extern "C" AudioStream *Audio_CreateStream(DWORD sampleRate, int channels)
{
    (void)sampleRate;
    (void)channels;
    return NULL;
}

extern "C" BOOL Audio_QueueStream(AudioStream *stream, const float *samples, DWORD sampleCount)
{
    (void)stream;
    (void)samples;
    (void)sampleCount;
    return FALSE;
}

extern "C" void Audio_StartStream(AudioStream *stream)
{
    (void)stream;
}

extern "C" void Audio_ReleaseStream(AudioStream *stream)
{
    (void)stream;
}
