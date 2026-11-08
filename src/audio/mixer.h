// Portable fixed-point software mixer behind the Amiga audio backend.
//
// DirectSound-style voices are summed into one stereo block. Sample data stays
// little-endian as loaded from the WAV files and is converted when it is read.
// All arithmetic is integer, so the mixer task never touches the FPU.
//
// Nothing here locks: the caller serialises the voice calls and Mixer_Render.
#ifndef OPENCMR2_AUDIO_MIXER_H
#define OPENCMR2_AUDIO_MIXER_H

#include <stdint.h>

enum { MIXER_MAX_BLOCK_FRAMES = 2048 };

struct MixSample {
    const uint8_t *bytes; // 8-bit unsigned or 16-bit little-endian PCM
    uint32_t frames;
    int bits;
    int channels;
};

struct MixVoice {
    MixVoice *prev;
    MixVoice *next;
    const MixSample *sample;
    uint32_t frame;
    uint32_t frac;          // 0..65535
    uint32_t stepWhole;
    uint32_t stepFrac;
    uint32_t frequency;
    int32_t volume;         // hundredths of a decibel
    int32_t pan;
    int32_t gainLeft;       // Q23, so a ramp keeps its fraction
    int32_t gainRight;
    int32_t deltaLeft;
    int32_t deltaRight;
    int32_t targetLeft;
    int32_t targetRight;
    uint32_t rampLeft;      // frames of gain ramp still to play
    bool playing;
    bool loop;
};

// A queued stream (movie sound): a power-of-two ring of native interleaved
// 16-bit frames. The writer appends behind the play position and publishes the
// new total with MixStream_Commit; the mixer never reads at or past `written`.
// When it gets there it plays silence, counts one underrun, and resumes as soon
// as more frames are committed. Both counters are monotonic and wrap at 2^32;
// only their difference is meaningful.
struct MixStream {
    MixStream *prev;
    MixStream *next;
    int16_t *ring;
    uint32_t mask;          // ring frames - 1
    int channels;           // 1 or 2
    uint32_t frequency;
    uint32_t stepWhole;
    uint32_t stepFrac;
    uint32_t written;       // frames committed by the writer
    uint32_t consumed;      // frames played (whole part of the position)
    uint32_t frac;          // 0..65535
    uint32_t underruns;
    int32_t lastLeft;       // the last frame mixed, handed to the declick
    int32_t lastRight;      // when the stream stops or runs dry
    bool playing;
    bool starved;
};

struct Mixer {
    MixVoice *voices;
    MixStream *streams;
    uint32_t rate;
    bool paused;
    int32_t declickLeft;    // decaying remainder of voices that were cut off
    int32_t declickRight;
    int32_t limiterGain;    // Q12
    uint32_t limitedBlocks;
    int32_t accumulator[MIXER_MAX_BLOCK_FRAMES * 2];
};

void Mixer_Init(Mixer *mixer, uint32_t outputRate);
void MixVoice_Init(MixVoice *voice, const MixSample *sample, uint32_t frequency);

// Keeps the position, as DirectSound does; a finished voice restarts from 0.
void Mixer_Play(Mixer *mixer, MixVoice *voice, bool loop);
void Mixer_Stop(Mixer *mixer, MixVoice *voice);
void Mixer_SetVolumePan(Mixer *mixer, MixVoice *voice, int32_t volume, int32_t pan);
void Mixer_SetFrequency(Mixer *mixer, MixVoice *voice, uint32_t frequency);
void Mixer_SetPosition(MixVoice *voice, uint32_t frame);

// ringFrames must be a power of two; ring holds ringFrames * channels samples.
void MixStream_Init(MixStream *stream, int16_t *ring, uint32_t ringFrames, int channels, uint32_t frequency);
// Frames the writer may append now, starting at ring frame `written & mask`.
uint32_t MixStream_Free(const MixStream *stream);
// Converts interleaved float samples (frames * channels values, full scale
// +-1.0) into the ring behind `written`, rounded to nearest and clamped; NaN
// becomes 0. Writes at most `room` frames (from MixStream_Free) and returns how
// many it wrote. It does not publish them, so it needs no lock and runs on the
// writer's side (it uses the FPU; the mixer task never calls it).
uint32_t MixStream_WriteFloat(MixStream *stream, const float *samples, uint32_t frames, uint32_t room);
// Publishes frames written into the ring.
void MixStream_Commit(MixStream *stream, uint32_t frames);
void Mixer_PlayStream(Mixer *mixer, MixStream *stream);
// Stops at once; the declick fades the last frame out.
void Mixer_StopStream(Mixer *mixer, MixStream *stream);

// Writes frames (at most MIXER_MAX_BLOCK_FRAMES) of native interleaved stereo.
void Mixer_Render(Mixer *mixer, int16_t *output, uint32_t frames);

#endif
