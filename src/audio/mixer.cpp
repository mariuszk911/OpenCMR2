#include "audio/mixer.h"

#include <math.h>
#include <string.h>

namespace {

enum {
    GAIN_SHIFT = 8,             // voice gain Q23 -> Q15
    ACCUMULATOR_SHIFT = 7,      // sample * Q15 gain >> 8 leaves 7 guard bits
    RAMP_FRAMES = 64,           // about 3 ms at 22 kHz, inaudible as a fade
    ATTENUATION_STEP_SHIFT = 4, // table step: 0.16 dB
    ATTENUATION_ENTRIES = (10000 >> ATTENUATION_STEP_SHIFT) + 1,
    LIMITER_UNITY = 4096,
    LIMITER_CEILING = 31000,
    LIMITER_INPUT_MAX = 262143, // keeps input * Q12 gain inside 32 bits
    DECLICK_SHIFT = 6
};

enum Filter { FILTER_NEAREST, FILTER_LINEAR, FILTER_BOX };

int16_t s_attenuation[ATTENUATION_ENTRIES];

// Attenuation in hundredths of a decibel to a Q15 gain. Decibels add, so the
// volume and pan laws need one table lookup and no multiplication.
int32_t GainFor(int32_t hundredths)
{
    if (hundredths >= 10000)
        return 0;
    return s_attenuation[hundredths >> ATTENUATION_STEP_SHIFT];
}

template <int Bits>
inline int32_t ReadPcm(const uint8_t *sample);

template <>
inline int32_t ReadPcm<8>(const uint8_t *sample)
{
    return ((int32_t)sample[0] - 128) * 256;
}

template <>
inline int32_t ReadPcm<16>(const uint8_t *sample)
{
    return (int16_t)(uint16_t)(sample[0] | (uint16_t)sample[1] << 8);
}

inline int32_t Scale(int32_t sample, int32_t gain)
{
    return (int32_t)(int16_t)sample * (int32_t)(int16_t)gain >> GAIN_SHIFT;
}

// One channel of the source at the voice position. first and next address the
// same channel in two adjacent frames.
template <int Bits, int Mode>
inline int32_t Filtered(const uint8_t *first, const uint8_t *next, uint32_t frac)
{
    const int32_t a = ReadPcm<Bits>(first);
    if (Mode == FILTER_NEAREST)
        return a;
    const int32_t b = ReadPcm<Bits>(next);
    if (Mode == FILTER_BOX)
        return (a + b) >> 1;
    return a + ((b - a) * (int32_t)(frac >> 4) >> 12);
}

// Plays frames that do not touch the last source frame, so `frame + 1` is
// always valid and no end check is needed per sample.
template <int Bits, int Channels, int Mode, bool Ramp>
void MixRun(MixVoice *voice, int32_t *out, uint32_t count)
{
    const int frameBytes = Bits / 8 * Channels;
    const int rightOffset = Bits / 8 * (Channels - 1);
    const uint8_t *bytes = voice->sample->bytes;
    const uint32_t stepWhole = voice->stepWhole;
    const uint32_t stepFrac = voice->stepFrac;
    uint32_t frame = voice->frame;
    uint32_t frac = voice->frac;
    int32_t gainLeft = voice->gainLeft;
    int32_t gainRight = voice->gainRight;
    const int32_t deltaLeft = voice->deltaLeft;
    const int32_t deltaRight = voice->deltaRight;

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *first = bytes + frame * frameBytes;
        const uint8_t *next = first + frameBytes;
        const int32_t left = Filtered<Bits, Mode>(first, next, frac);
        const int32_t right = Channels == 2
            ? Filtered<Bits, Mode>(first + rightOffset, next + rightOffset, frac)
            : left;
        out[0] += Scale(left, gainLeft >> GAIN_SHIFT);
        out[1] += Scale(right, gainRight >> GAIN_SHIFT);
        out += 2;
        if (Ramp) {
            gainLeft += deltaLeft;
            gainRight += deltaRight;
        }
        frac += stepFrac;
        frame += stepWhole + (frac >> 16);
        frac &= 0xffffu;
    }
    voice->frame = frame;
    voice->frac = frac;
    voice->gainLeft = gainLeft;
    voice->gainRight = gainRight;
    if (Ramp) {
        voice->rampLeft -= count;
        if (voice->rampLeft == 0) {
            // The truncated deltas stop just short: land on the exact target,
            // so a fade to zero reaches the silent path.
            voice->gainLeft = voice->targetLeft;
            voice->gainRight = voice->targetRight;
            voice->deltaLeft = 0;
            voice->deltaRight = 0;
        }
    }
}

typedef void (*MixRunFunc)(MixVoice *, int32_t *, uint32_t);

template <int Bits, int Channels, int Mode>
MixRunFunc SelectRun(bool ramp)
{
    return ramp ? MixRun<Bits, Channels, Mode, true> : MixRun<Bits, Channels, Mode, false>;
}

template <int Bits, int Channels>
MixRunFunc SelectRun(int mode, bool ramp)
{
    if (mode == FILTER_NEAREST)
        return SelectRun<Bits, Channels, FILTER_NEAREST>(ramp);
    if (mode == FILTER_BOX)
        return SelectRun<Bits, Channels, FILTER_BOX>(ramp);
    return SelectRun<Bits, Channels, FILTER_LINEAR>(ramp);
}

MixRunFunc SelectRun(const MixVoice *voice, int mode, bool ramp)
{
    const MixSample *sample = voice->sample;
    if (sample->bits == 16)
        return sample->channels == 2 ? SelectRun<16, 2>(mode, ramp) : SelectRun<16, 1>(mode, ramp);
    return sample->channels == 2 ? SelectRun<8, 2>(mode, ramp) : SelectRun<8, 1>(mode, ramp);
}

int32_t ChannelAt(const MixSample *sample, uint32_t frame, int channel)
{
    const int bytes = sample->bits / 8;
    const uint8_t *p = sample->bytes + (frame * sample->channels + (sample->channels == 2 ? channel : 0)) * bytes;
    return bytes == 2 ? ReadPcm<16>(p) : ReadPcm<8>(p);
}

void Link(Mixer *mixer, MixVoice *voice)
{
    voice->prev = NULL;
    voice->next = mixer->voices;
    if (mixer->voices != NULL)
        mixer->voices->prev = voice;
    mixer->voices = voice;
}

void Unlink(Mixer *mixer, MixVoice *voice)
{
    if (voice->prev != NULL)
        voice->prev->next = voice->next;
    else
        mixer->voices = voice->next;
    if (voice->next != NULL)
        voice->next->prev = voice->prev;
    voice->prev = NULL;
    voice->next = NULL;
}

void TargetGains(const MixVoice *voice, int32_t *left, int32_t *right)
{
    const int32_t attenuation = -voice->volume;
    *left = GainFor(attenuation + (voice->pan > 0 ? voice->pan : 0));
    *right = GainFor(attenuation + (voice->pan < 0 ? -voice->pan : 0));
}

void UpdateStep(const Mixer *mixer, MixVoice *voice)
{
    const uint32_t remainder = voice->frequency % mixer->rate;
    voice->stepWhole = voice->frequency / mixer->rate;
    voice->stepFrac = (remainder << 16) / mixer->rate;
    if (voice->stepWhole == 0 && voice->stepFrac == 0)
        voice->stepFrac = 1;
}

// A voice that stops mid-waveform leaves a step in the output. Keeping its last
// value and letting it decay removes the click without touching the sample
// again, so the buffer can be released at once.
void HandOverToDeclick(Mixer *mixer, const MixVoice *voice)
{
    const MixSample *sample = voice->sample;
    const uint32_t frame = voice->frame < sample->frames ? voice->frame : sample->frames - 1;
    mixer->declickLeft += Scale(ChannelAt(sample, frame, 0), voice->gainLeft >> GAIN_SHIFT);
    mixer->declickRight += Scale(ChannelAt(sample, frame, 1), voice->gainRight >> GAIN_SHIFT);
}

void Finish(Mixer *mixer, MixVoice *voice)
{
    HandOverToDeclick(mixer, voice);
    Unlink(mixer, voice);
    voice->playing = false;
    voice->frame = 0;
    voice->frac = 0;
}

void Advance(MixVoice *voice, uint32_t count)
{
    const uint64_t step = ((uint64_t)voice->stepWhole << 16) + voice->stepFrac;
    const uint64_t moved = voice->frac + step * count;
    voice->frame += (uint32_t)(moved >> 16);
    voice->frac = (uint32_t)moved & 0xffffu;
}

// The last frame interpolates towards the loop start, or holds if there is none.
void MixLastFrame(MixVoice *voice, int32_t *out)
{
    const MixSample *sample = voice->sample;
    const uint32_t next = voice->loop ? 0 : voice->frame;
    const int32_t frac = (int32_t)(voice->frac >> 4);
    for (int channel = 0; channel < 2; ++channel) {
        const int32_t a = ChannelAt(sample, voice->frame, channel);
        const int32_t b = ChannelAt(sample, next, channel);
        const int32_t gain = channel == 0 ? voice->gainLeft : voice->gainRight;
        out[channel] += Scale(a + ((b - a) * frac >> 12), gain >> GAIN_SHIFT);
    }
    Advance(voice, 1);
}

int FilterFor(const MixVoice *voice)
{
    // At twice the source rate or more, linear interpolation only picks
    // samples and the music ring (44.1 kHz) aliases into a 22 kHz output.
    if (voice->stepWhole >= 2)
        return FILTER_BOX;
    if (voice->stepWhole == 1 && voice->stepFrac == 0 && voice->frac == 0)
        return FILTER_NEAREST;
    return FILTER_LINEAR;
}

void MixVoiceBlock(Mixer *mixer, MixVoice *voice, int32_t *out, uint32_t frames)
{
    const uint32_t total = voice->sample->frames;
    const uint32_t step = (voice->stepWhole << 16) + voice->stepFrac;

    while (frames != 0) {
        if (voice->frame >= total) {
            if (!voice->loop) {
                Finish(mixer, voice);
                return;
            }
            voice->frame %= total;
        }
        if (voice->frame + 1 >= total) {
            MixLastFrame(voice, out);
            out += 2;
            --frames;
            continue;
        }
        // Capped so the fixed-point distance fits in 32 bits; a longer sample
        // comes back here for the next run.
        uint32_t room = total - 1 - voice->frame;
        room = room > 0x7fffu ? 0x7fffu : room;
        uint32_t count = ((room << 16) - voice->frac + step - 1) / step;
        count = count > frames ? frames : count;

        const bool ramp = voice->rampLeft != 0;
        if (ramp && count > voice->rampLeft)
            count = voice->rampLeft;
        if (!ramp && voice->gainLeft == 0 && voice->gainRight == 0)
            Advance(voice, count);
        else
            SelectRun(voice, FilterFor(voice), ramp)(voice, out, count);
        out += count * 2;
        frames -= count;
    }
}

void AddDeclick(int32_t *accumulator, int32_t *remainder, uint32_t frames)
{
    int32_t value = *remainder;
    for (uint32_t i = 0; i < frames && value != 0; ++i) {
        accumulator[i * 2] += value;
        value -= value / (1 << DECLICK_SHIFT) + (value > 0 ? 1 : -1);
    }
    *remainder = value;
}

int32_t Magnitude(int32_t value)
{
    return value < 0 ? -value : value;
}

// Summed voices can pass full scale. Clipping them is what crunches, so the
// block is measured first and the gain moves before the loud part is written.
// Output stays clean; only a peak inside the first ramp can reach the clamp.
void WriteLimited(Mixer *mixer, int16_t *output, uint32_t frames)
{
    const int32_t *in = mixer->accumulator;
    int32_t peak = 0;
    for (uint32_t i = 0; i < frames * 2; ++i) {
        const int32_t level = Magnitude(in[i]);
        if (level > peak)
            peak = level;
    }
    peak >>= ACCUMULATOR_SHIFT;
    peak = peak > LIMITER_INPUT_MAX ? LIMITER_INPUT_MAX : peak;

    const int32_t wanted = peak > LIMITER_CEILING ? (LIMITER_CEILING * LIMITER_UNITY) / peak : LIMITER_UNITY;
    const int32_t start = mixer->limiterGain;
    int32_t end = wanted;
    uint32_t ramp = frames < RAMP_FRAMES ? frames : RAMP_FRAMES;
    if (wanted > start) {
        const int32_t release = ((LIMITER_UNITY - start) >> 3) + 1;
        end = start + release < wanted ? start + release : wanted;
        ramp = frames;
    }
    if (wanted < LIMITER_UNITY)
        ++mixer->limitedBlocks;

    int32_t gain = start * 65536;
    const int32_t delta = ramp != 0 ? (end - start) * 65536 / (int32_t)ramp : 0;
    for (uint32_t i = 0; i < frames; ++i) {
        if (i < ramp)
            gain += delta;
        else
            gain = end * 65536;
        const int32_t current = gain >> 16;
        for (int channel = 0; channel < 2; ++channel) {
            int32_t value = in[i * 2 + channel] >> ACCUMULATOR_SHIFT;
            value = value > LIMITER_INPUT_MAX ? LIMITER_INPUT_MAX : (value < -LIMITER_INPUT_MAX ? -LIMITER_INPUT_MAX : value);
            value = value * current >> 12;
            output[i * 2 + channel] = (int16_t)(value > 32767 ? 32767 : (value < -32768 ? -32768 : value));
        }
    }
    mixer->limiterGain = end;
}

// Streams play at unity gain; the sample at `pos` is read as the frame at the
// ring index, mono to both sides.
inline void StreamFrame(const MixStream *stream, uint32_t pos, int32_t *left, int32_t *right)
{
    const int16_t *p = stream->ring + (size_t)(pos & stream->mask) * (size_t)stream->channels;
    *left = p[0];
    *right = stream->channels == 2 ? p[1] : p[0];
}

void StreamStep(const Mixer *mixer, MixStream *stream)
{
    const uint32_t frequency = stream->frequency != 0 ? stream->frequency : mixer->rate;
    stream->stepWhole = frequency / mixer->rate;
    stream->stepFrac = ((frequency % mixer->rate) << 16) / mixer->rate;
    if (stream->stepWhole == 0 && stream->stepFrac == 0)
        stream->stepFrac = 1;
}

void StreamToDeclick(Mixer *mixer, MixStream *stream)
{
    const int32_t unity = GainFor(0);
    mixer->declickLeft += Scale(stream->lastLeft, unity);
    mixer->declickRight += Scale(stream->lastRight, unity);
    stream->lastLeft = 0;
    stream->lastRight = 0;
}

// The position may step past `written` (a step above one frame); the signed
// difference then reads as "nothing left", and the next commit covers it.
void MixStreamBlock(Mixer *mixer, MixStream *stream, int32_t *out, uint32_t frames)
{
    const int32_t unity = GainFor(0);
    const uint32_t stepWhole = stream->stepWhole;
    const uint32_t stepFrac = stream->stepFrac;
    const bool box = stepWhole >= 2;
    uint32_t pos = stream->consumed;
    uint32_t frac = stream->frac;
    int32_t left = stream->lastLeft;
    int32_t right = stream->lastRight;

    for (uint32_t i = 0; i < frames; ++i) {
        const int32_t available = (int32_t)(stream->written - pos);
        if (available <= 0) {
            if (!stream->starved) {
                // Fade from here, not from the block start the shared declick
                // uses, and hand what is left to it for the next block.
                int32_t fadeLeft = Scale(left, unity);
                int32_t fadeRight = Scale(right, unity);
                AddDeclick(out + i * 2, &fadeLeft, frames - i);
                AddDeclick(out + i * 2 + 1, &fadeRight, frames - i);
                mixer->declickLeft += fadeLeft;
                mixer->declickRight += fadeRight;
                stream->starved = true;
                ++stream->underruns;
                stream->lastLeft = 0;
                stream->lastRight = 0;
            }
            stream->consumed = pos;
            stream->frac = frac;
            return;
        }
        stream->starved = false;
        int32_t aLeft, aRight, bLeft, bRight;
        StreamFrame(stream, pos, &aLeft, &aRight);
        if (available > 1)
            StreamFrame(stream, pos + 1, &bLeft, &bRight);
        else {
            bLeft = aLeft;
            bRight = aRight;
        }
        if (box) {
            left = (aLeft + bLeft) >> 1;
            right = (aRight + bRight) >> 1;
        } else {
            const int32_t f = (int32_t)(frac >> 4);
            left = aLeft + ((bLeft - aLeft) * f >> 12);
            right = aRight + ((bRight - aRight) * f >> 12);
        }
        out[i * 2] += Scale(left, unity);
        out[i * 2 + 1] += Scale(right, unity);
        frac += stepFrac;
        pos += stepWhole + (frac >> 16);
        frac &= 0xffffu;
    }
    stream->consumed = pos;
    stream->frac = frac;
    stream->lastLeft = left;
    stream->lastRight = right;
}

} // namespace

void Mixer_Init(Mixer *mixer, uint32_t outputRate)
{
    memset(mixer, 0, sizeof(*mixer));
    mixer->rate = outputRate;
    mixer->limiterGain = LIMITER_UNITY;
    for (int i = 0; i < ATTENUATION_ENTRIES; ++i) {
        const double gain = pow(10.0, -(double)(i << ATTENUATION_STEP_SHIFT) / 2000.0);
        s_attenuation[i] = (int16_t)(gain * 32767.0 + 0.5);
    }
}

void MixVoice_Init(MixVoice *voice, const MixSample *sample, uint32_t frequency)
{
    memset(voice, 0, sizeof(*voice));
    voice->sample = sample;
    voice->frequency = frequency;
}

void Mixer_Play(Mixer *mixer, MixVoice *voice, bool loop)
{
    voice->loop = loop;
    if (voice->playing || voice->sample->frames == 0)
        return;
    int32_t left;
    int32_t right;
    TargetGains(voice, &left, &right);
    voice->gainLeft = left * (1 << GAIN_SHIFT);
    voice->gainRight = right * (1 << GAIN_SHIFT);
    voice->deltaLeft = 0;
    voice->deltaRight = 0;
    voice->rampLeft = 0;
    UpdateStep(mixer, voice);
    voice->playing = true;
    Link(mixer, voice);
}

void Mixer_Stop(Mixer *mixer, MixVoice *voice)
{
    if (!voice->playing)
        return;
    HandOverToDeclick(mixer, voice);
    Unlink(mixer, voice);
    voice->playing = false;
}

void Mixer_SetVolumePan(Mixer *mixer, MixVoice *voice, int32_t volume, int32_t pan)
{
    (void)mixer;
    voice->volume = volume;
    voice->pan = pan;
    if (!voice->playing)
        return;
    int32_t left;
    int32_t right;
    TargetGains(voice, &left, &right);
    voice->targetLeft = left * (1 << GAIN_SHIFT);
    voice->targetRight = right * (1 << GAIN_SHIFT);
    voice->deltaLeft = (voice->targetLeft - voice->gainLeft) / RAMP_FRAMES;
    voice->deltaRight = (voice->targetRight - voice->gainRight) / RAMP_FRAMES;
    voice->rampLeft = voice->deltaLeft != 0 || voice->deltaRight != 0 ? RAMP_FRAMES : 0;
}

void Mixer_SetFrequency(Mixer *mixer, MixVoice *voice, uint32_t frequency)
{
    voice->frequency = frequency;
    UpdateStep(mixer, voice);
}

void Mixer_SetPosition(MixVoice *voice, uint32_t frame)
{
    voice->frame = frame;
    voice->frac = 0;
}

void MixStream_Init(MixStream *stream, int16_t *ring, uint32_t ringFrames, int channels, uint32_t frequency)
{
    memset(stream, 0, sizeof(*stream));
    stream->ring = ring;
    stream->mask = ringFrames - 1;
    stream->channels = channels;
    stream->frequency = frequency;
}

uint32_t MixStream_Free(const MixStream *stream)
{
    const int32_t queued = (int32_t)(stream->written - stream->consumed);
    const uint32_t capacity = stream->mask + 1;
    return queued <= 0 ? capacity : capacity - (uint32_t)queued;
}

uint32_t MixStream_WriteFloat(MixStream *stream, const float *samples, uint32_t frames, uint32_t room)
{
    const uint32_t count = frames < room ? frames : room;
    const int channels = stream->channels;
    for (uint32_t i = 0; i < count; ++i) {
        int16_t *dst = stream->ring + (size_t)((stream->written + i) & stream->mask) * (size_t)channels;
        for (int c = 0; c < channels; ++c) {
            float x = samples[(size_t)i * (size_t)channels + (size_t)c];
            if (x != x)
                x = 0.0f;
            x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
            x *= 32767.0f;
            // Truncation after the half offset rounds to nearest, halves away
            // from zero; the clamp keeps the value inside int16.
            dst[c] = (int16_t)(int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
        }
    }
    return count;
}

void MixStream_Commit(MixStream *stream, uint32_t frames)
{
    stream->written += frames;
}

void Mixer_PlayStream(Mixer *mixer, MixStream *stream)
{
    if (stream->playing)
        return;
    StreamStep(mixer, stream);
    stream->playing = true;
    stream->prev = NULL;
    stream->next = mixer->streams;
    if (mixer->streams != NULL)
        mixer->streams->prev = stream;
    mixer->streams = stream;
}

void Mixer_StopStream(Mixer *mixer, MixStream *stream)
{
    if (!stream->playing)
        return;
    StreamToDeclick(mixer, stream);
    if (stream->prev != NULL)
        stream->prev->next = stream->next;
    else
        mixer->streams = stream->next;
    if (stream->next != NULL)
        stream->next->prev = stream->prev;
    stream->prev = NULL;
    stream->next = NULL;
    stream->playing = false;
}

void Mixer_Render(Mixer *mixer, int16_t *output, uint32_t frames)
{
    if (mixer->paused) {
        memset(output, 0, frames * 2 * sizeof(int16_t));
        return;
    }
    memset(mixer->accumulator, 0, frames * 2 * sizeof(int32_t));
    MixVoice *voice = mixer->voices;
    while (voice != NULL) {
        MixVoice *next = voice->next; // the voice unlinks itself when it ends
        MixVoiceBlock(mixer, voice, mixer->accumulator, frames);
        voice = next;
    }
    AddDeclick(mixer->accumulator, &mixer->declickLeft, frames);
    AddDeclick(mixer->accumulator + 1, &mixer->declickRight, frames);
    // After the declick: a stream that runs dry fades out from that frame
    // itself and leaves the rest of its fade to the next block.
    for (MixStream *stream = mixer->streams; stream != NULL; stream = stream->next)
        MixStreamBlock(mixer, stream, mixer->accumulator, frames);
    WriteLimited(mixer, output, frames);
}
