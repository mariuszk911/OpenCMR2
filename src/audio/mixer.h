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

struct Mixer {
    MixVoice *voices;
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

// Writes frames (at most MIXER_MAX_BLOCK_FRAMES) of native interleaved stereo.
void Mixer_Render(Mixer *mixer, int16_t *output, uint32_t frames);

#endif
