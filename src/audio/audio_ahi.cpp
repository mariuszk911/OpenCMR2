// Audio on AmigaOS. The mixer/AHI output is not wired yet; buffers exist and
// accept PCM so the game can load and queue sound without crashing. MS-ADPCM
// decoding is the portable implementation shared with the host.
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
}

extern "C" void Audio_Stop(AudioBuffer *buffer)
{
    if (buffer != NULL)
        buffer->playing = 0;
}

extern "C" int Audio_IsPlaying(AudioBuffer *buffer)
{
    return buffer != NULL && buffer->playing;
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
    static const int adaptation[16] = { 230, 230, 230, 230, 307, 409, 512, 614,
                                        768, 614, 512, 409, 307, 230, 230, 230 };
    static const short standardCoefs[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 },
                                               { 240, 0 }, { 460, -208 }, { 392, -232 } };
    short coefs[32][2];
    int coefCount = 7;
    int channels = format->nChannels;
    DWORD blockAlign = format->nBlockAlign;
    DWORD written = 0;

    memcpy(coefs, standardCoefs, sizeof(standardCoefs));
    if (format->cbSize >= 4) {
        const BYTE *extra = (const BYTE *)format + sizeof(AudioWaveFormat);
        WORD count = (WORD)(extra[2] | extra[3] << 8);
        if (count >= 7 && count <= 32 && format->cbSize >= 4 + count * 4) {
            coefCount = count;
            for (int i = 0; i < count; i++) {
                coefs[i][0] = (short)(extra[4 + i * 4] | extra[5 + i * 4] << 8);
                coefs[i][1] = (short)(extra[6 + i * 4] | extra[7 + i * 4] << 8);
            }
        }
    }
    if (channels < 1 || channels > 2 || blockAlign < (DWORD)(7 * channels))
        return 0;

    int16_t *out = (int16_t *)dst;
    DWORD outMax = dstBytes / 2;
    for (DWORD offset = 0; offset + blockAlign <= srcBytes; offset += blockAlign) {
        const BYTE *p = src + offset;
        int predictor[2], delta[2], s1[2], s2[2];
        for (int c = 0; c < channels; c++)
            predictor[c] = p[c] < coefCount ? p[c] : 0;
        p += channels;
        for (int c = 0; c < channels; c++, p += 2)
            delta[c] = (int16_t)(p[0] | p[1] << 8);
        for (int c = 0; c < channels; c++, p += 2)
            s1[c] = (int16_t)(p[0] | p[1] << 8);
        for (int c = 0; c < channels; c++, p += 2)
            s2[c] = (int16_t)(p[0] | p[1] << 8);
        for (int c = 0; c < channels && written < outMax; c++)
            out[written++] = (int16_t)s2[c];
        for (int c = 0; c < channels && written < outMax; c++)
            out[written++] = (int16_t)s1[c];
        const BYTE *end = src + offset + blockAlign;
        int channel = 0;
        for (; p < end && written < outMax; p++) {
            for (int half = 0; half < 2 && written < outMax; half++) {
                int nibble = half == 0 ? p[0] >> 4 : p[0] & 0xf;
                int signedNibble = nibble >= 8 ? nibble - 16 : nibble;
                int c = channel;
                int prediction = (s1[c] * coefs[predictor[c]][0] + s2[c] * coefs[predictor[c]][1]) / 256;
                int sample = prediction + signedNibble * delta[c];
                sample = sample > 32767 ? 32767 : (sample < -32768 ? -32768 : sample);
                out[written++] = (int16_t)sample;
                s2[c] = s1[c];
                s1[c] = sample;
                delta[c] = delta[c] * adaptation[nibble] / 256;
                if (delta[c] < 16)
                    delta[c] = 16;
                channel = (channel + 1) % channels;
            }
        }
    }
    return written * 2;
}
