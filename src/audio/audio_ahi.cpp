// Audio on AmigaOS. The mixer/AHI output is not wired yet; buffers exist and
// accept PCM so the game can load and queue sound without crashing. A one-shot
// buffer still "plays" for its real duration in silence, so the game's sound
// slots and speech queues are released as they would be with output.
#include "port/audio.h"
#include "port/sys.h"

#include <stdlib.h>
#include <string.h>

struct AudioBuffer {
    DWORD sampleRate;
    int bits;
    int channels;
    DWORD bytes;
    unsigned char *data;
    int playing;
    int loop;
    LONG volume;
    LONG pan;
    DWORD frequency;
    DWORD position;
    uint32_t startTicks; // Sys_GetTicks() at Audio_Play
};

struct AudioStream {
    DWORD sampleRate;
    int channels;
};

extern "C" int Audio_Init(int sampleRate, int channels)
{
    (void)sampleRate;
    (void)channels;
    return 1;
}

extern "C" void Audio_Shutdown(void)
{
}

extern "C" void Audio_SetPaused(int paused)
{
    (void)paused;
}

extern "C" AudioBuffer *Audio_CreateBuffer(DWORD sampleRate, int bits, int channels, DWORD bytes)
{
    AudioBuffer *buffer = (AudioBuffer *)calloc(1, sizeof(AudioBuffer));
    if (buffer == NULL)
        return NULL;
    buffer->data = (unsigned char *)calloc(1, bytes != 0 ? bytes : 1);
    if (buffer->data == NULL) {
        free(buffer);
        return NULL;
    }
    buffer->sampleRate = sampleRate;
    buffer->bits = bits;
    buffer->channels = channels;
    buffer->bytes = bytes;
    buffer->volume = AUDIO_VOLUME_MAX;
    buffer->frequency = sampleRate;
    return buffer;
}

extern "C" AudioBuffer *Audio_DuplicateBuffer(AudioBuffer *source)
{
    if (source == NULL)
        return NULL;
    AudioBuffer *buffer = Audio_CreateBuffer(source->sampleRate, source->bits, source->channels,
                                             source->bytes);
    if (buffer == NULL)
        return NULL;
    memcpy(buffer->data, source->data, source->bytes);
    buffer->volume = source->volume;
    buffer->pan = source->pan;
    buffer->frequency = source->frequency;
    return buffer;
}

extern "C" void Audio_ReleaseBuffer(AudioBuffer *buffer)
{
    if (buffer == NULL)
        return;
    free(buffer->data);
    free(buffer);
}

extern "C" DWORD Audio_GetBufferSize(AudioBuffer *buffer)
{
    return buffer != NULL ? buffer->bytes : 0;
}

extern "C" void Audio_GetBufferFormat(AudioBuffer *buffer, DWORD *sampleRate, int *bits, int *channels)
{
    if (buffer == NULL)
        return;
    if (sampleRate != NULL)
        *sampleRate = buffer->sampleRate;
    if (bits != NULL)
        *bits = buffer->bits;
    if (channels != NULL)
        *channels = buffer->channels;
}

extern "C" int Audio_LockBuffer(AudioBuffer *buffer, DWORD offset, DWORD bytes, void **p1, DWORD *n1,
                                void **p2, DWORD *n2)
{
    if (buffer == NULL || p1 == NULL || n1 == NULL || p2 == NULL || n2 == NULL || buffer->bytes == 0)
        return 0;
    if (offset >= buffer->bytes)
        offset %= buffer->bytes;
    const DWORD first = bytes < buffer->bytes - offset ? bytes : buffer->bytes - offset;
    const DWORD second = bytes - first < buffer->bytes ? bytes - first : buffer->bytes;
    *p1 = buffer->data + offset;
    *n1 = first;
    *p2 = buffer->data;
    *n2 = second;
    return 1;
}

extern "C" void Audio_UnlockBuffer(AudioBuffer *buffer)
{
    (void)buffer;
}

extern "C" void Audio_Play(AudioBuffer *buffer, int loop)
{
    if (buffer == NULL)
        return;
    buffer->playing = 1;
    buffer->loop = loop != 0;
    buffer->position = 0;
    buffer->startTicks = Sys_GetTicks();
}

extern "C" void Audio_Stop(AudioBuffer *buffer)
{
    if (buffer != NULL)
        buffer->playing = 0;
}

namespace {

// Milliseconds a one-shot buffer takes at its current frequency.
uint32_t PlayDuration(const AudioBuffer *buffer)
{
    const uint64_t bytesPerSecond =
        (uint64_t)buffer->frequency * (uint64_t)buffer->channels * (uint64_t)(buffer->bits / 8);
    if (bytesPerSecond == 0)
        return 0;
    const uint64_t ms = (uint64_t)buffer->bytes * 1000u / bytesPerSecond;
    return ms > 0xffffffffu ? 0xffffffffu : (uint32_t)ms;
}

} // namespace

extern "C" int Audio_IsPlaying(AudioBuffer *buffer)
{
    if (buffer == NULL || !buffer->playing)
        return 0;
    // Unsigned subtraction keeps the elapsed time right across the 2^32 wrap.
    if (!buffer->loop && Sys_GetTicks() - buffer->startTicks >= PlayDuration(buffer))
        buffer->playing = 0;
    return buffer->playing;
}

extern "C" DWORD Audio_GetPlayPosition(AudioBuffer *buffer)
{
    return buffer != NULL ? buffer->position : 0;
}

extern "C" void Audio_SetPlayPosition(AudioBuffer *buffer, DWORD position)
{
    if (buffer != NULL)
        buffer->position = position;
}

extern "C" void Audio_SetVolume(AudioBuffer *buffer, LONG volume)
{
    if (buffer != NULL)
        buffer->volume = volume;
}

extern "C" void Audio_SetPan(AudioBuffer *buffer, LONG pan)
{
    if (buffer != NULL)
        buffer->pan = pan;
}

extern "C" void Audio_SetFrequency(AudioBuffer *buffer, DWORD frequency)
{
    if (buffer != NULL)
        buffer->frequency = frequency;
}

extern "C" AudioStream *Audio_CreateStream(DWORD sampleRate, int channels)
{
    AudioStream *stream = (AudioStream *)calloc(1, sizeof(AudioStream));
    if (stream == NULL)
        return NULL;
    stream->sampleRate = sampleRate;
    stream->channels = channels;
    return stream;
}

extern "C" int Audio_QueueStream(AudioStream *stream, const float *samples, DWORD sampleCount)
{
    (void)stream;
    (void)samples;
    (void)sampleCount;
    return stream != NULL;
}

extern "C" void Audio_StartStream(AudioStream *stream)
{
    (void)stream;
}

extern "C" void Audio_ReleaseStream(AudioStream *stream)
{
    free(stream);
}

// ---- MS-ADPCM ---------------------------------------------------------------------

extern "C" DWORD Audio_DecodeMsAdpcm(const AudioWaveFormat *format, const BYTE *src, DWORD srcBytes, BYTE *dst,
                                     DWORD dstBytes)
{
    // Audio is out of scope until the AHI slice; do not decode or touch the
    // sample buffers so the frontend cannot crash in the temporary backend.
    (void)format;
    (void)src;
    (void)srcBytes;
    (void)dst;
    (void)dstBytes;
    return 0;
}
