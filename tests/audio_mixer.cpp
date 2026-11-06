// Fixed-point mixer behind the Amiga audio backend. The target compiles the
// same mixer.cpp and msadpcm.cpp, so the rules the game relies on are proved here.
#include "audio/mixer.h"
#include "port/audio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

Mixer mixer;

MixSample Pcm16(std::vector<uint8_t> &bytes, const std::vector<int> &values, int channels)
{
    bytes.clear();
    for (int value : values) {
        bytes.push_back((uint8_t)(value & 0xff));
        bytes.push_back((uint8_t)((value >> 8) & 0xff));
    }
    MixSample sample = { bytes.data(), (uint32_t)(values.size() / channels), 16, channels };
    return sample;
}

void LittleEndianSourceAndUnityGain()
{
    Mixer_Init(&mixer, 1000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, { 1000, -2000, 3000, 3000 }, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_Play(&mixer, &voice, false);
    int16_t out[4 * 2];
    Mixer_Render(&mixer, out, 4);
    Check(abs(out[0] - 1000) <= 1 && abs(out[2] + 2000) <= 1 && abs(out[4] - 3000) <= 1, "16-bit LE at unity");
    Check(out[0] == out[1], "mono reaches both sides");
}

void OneShotEndsAndRewinds()
{
    Mixer_Init(&mixer, 1000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, { 100, 100, 100 }, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_Play(&mixer, &voice, false);
    int16_t out[8 * 2];
    Mixer_Render(&mixer, out, 8);
    Check(!voice.playing && voice.frame == 0 && mixer.voices == NULL, "a one-shot retires and rewinds");
}

void LoopWrapsAtTheRightFrame()
{
    Mixer_Init(&mixer, 1000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, { 1000, 2000, 3000 }, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_Play(&mixer, &voice, true);
    int16_t out[7 * 2];
    Mixer_Render(&mixer, out, 7);
    Check(voice.playing && voice.frame == 1, "the loop keeps playing");
    Check(abs(out[6] - 1000) <= 1 && abs(out[12] - 1000) <= 1, "the loop restarts at frame zero");
}

void ResamplingAdvancesAtTheVoiceRate()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int> values(44100, 0);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 2);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 44100);
    Mixer_Play(&mixer, &voice, true);
    static int16_t out[MIXER_MAX_BLOCK_FRAMES * 2];
    Mixer_Render(&mixer, out, 1000);
    Check(voice.frame == 2000, "the ring cursor moves at the source rate");
}

void DownsamplingFiltersNyquist()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int> values;
    for (int i = 0; i < 400; ++i)
        values.push_back(i & 1 ? -20000 : 20000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 44100);
    Mixer_Play(&mixer, &voice, true);
    int16_t out[64 * 2];
    Mixer_Render(&mixer, out, 64);
    Check(abs(out[20]) < 100, "2:1 downsampling removes content the output cannot hold");
}

void SilentVoicesKeepPosition()
{
    Mixer_Init(&mixer, 1000);
    std::vector<int> values(5000, 500);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_SetVolumePan(&mixer, &voice, AUDIO_VOLUME_MIN, 0);
    Mixer_Play(&mixer, &voice, true);
    int16_t out[100 * 2];
    Mixer_Render(&mixer, out, 100);
    Check(voice.frame == 100 && out[0] == 0, "a muted voice still advances");
}

void PanFollowsTheDirectSoundLaw()
{
    Mixer_Init(&mixer, 1000);
    std::vector<int> values(1000, 20000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_SetVolumePan(&mixer, &voice, 0, 600); // -6 dB on the left
    Mixer_Play(&mixer, &voice, true);
    int16_t out[8 * 2];
    Mixer_Render(&mixer, out, 8);
    Check(abs(out[2] - 10024) < 120 && abs(out[3] - 20000) < 4, "pan attenuates the far side only");
}

void VolumeChangesRampWithoutSteps()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int> values(4000, 20000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 22050);
    Mixer_Play(&mixer, &voice, true);
    int16_t out[200 * 2];
    Mixer_Render(&mixer, out, 200);
    Mixer_SetVolumePan(&mixer, &voice, AUDIO_VOLUME_MIN, 0);
    Mixer_Render(&mixer, out, 200);
    int largest = 0;
    for (int i = 1; i < 200; ++i) {
        const int change = abs(out[i * 2] - out[(i - 1) * 2]);
        largest = change > largest ? change : largest;
    }
    Check(largest < 600, "a volume change has no step");
    Check(voice.gainLeft == 0 && out[300] == 0, "a fade lands exactly on silence");
}

void StoppingDoesNotClick()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int> values(4000, 20000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 22050);
    Mixer_Play(&mixer, &voice, true);
    int16_t out[1000 * 2];
    Mixer_Render(&mixer, out, 10);
    Mixer_Stop(&mixer, &voice);
    Mixer_Render(&mixer, out, 1000);
    Check(out[0] > 18000, "a cut voice decays from where it stopped");
    Check(out[1998] == 0 && mixer.declickLeft == 0, "the tail reaches silence");
}

void OverloadIsLimitedNotClipped()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int> values;
    for (int i = 0; i < 8000; ++i)
        values.push_back(i % 50 < 25 ? 30000 : -30000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    static MixVoice voices[16];
    for (MixVoice &voice : voices) {
        MixVoice_Init(&voice, &sample, 22050);
        Mixer_Play(&mixer, &voice, true);
    }
    static int16_t out[800 * 2];
    Mixer_Render(&mixer, out, 800);
    Mixer_Render(&mixer, out, 800);
    int peak = 0;
    for (int i = 0; i < 1600; ++i)
        peak = abs(out[i]) > peak ? abs(out[i]) : peak;
    Check(peak <= 31000 && peak > 29000, "sixteen full-scale voices stay under the ceiling");
    Check(mixer.limitedBlocks == 2, "the limiter reports its blocks");

    for (MixVoice &voice : voices)
        Mixer_Stop(&mixer, &voice);
    for (int i = 0; i < 60; ++i)
        Mixer_Render(&mixer, out, 800);
    Check(mixer.limiterGain == 4096, "the gain recovers");
}

void PausedHoldsPositions()
{
    Mixer_Init(&mixer, 1000);
    std::vector<int> values(100, 9000);
    std::vector<uint8_t> bytes;
    MixSample sample = Pcm16(bytes, values, 1);
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_Play(&mixer, &voice, true);
    mixer.paused = true;
    int16_t out[10 * 2];
    Mixer_Render(&mixer, out, 10);
    Check(voice.frame == 0 && out[0] == 0, "paused output is silent and holds position");
}

void Eight8BitAndStereo()
{
    Mixer_Init(&mixer, 1000);
    uint8_t pcm[] = { 0xc0, 0x40, 0xc0, 0x40, 0xc0, 0x40 };
    MixSample sample = { pcm, 3, 8, 2 };
    MixVoice voice;
    MixVoice_Init(&voice, &sample, 1000);
    Mixer_Play(&mixer, &voice, false);
    int16_t out[2 * 2];
    Mixer_Render(&mixer, out, 2);
    Check(abs(out[0] - 16384) <= 2 && abs(out[1] + 16384) <= 2, "8-bit unsigned stereo");
}

void DecoderWritesLittleEndian()
{
    AudioWaveFormat format = {};
    format.wFormatTag = AUDIO_WAVE_FORMAT_ADPCM;
    format.nChannels = 1;
    format.nBlockAlign = 8;
    const BYTE block[8] = { 0, 16, 0, 0x02, 0x01, 0x04, 0x03, 0x00 };
    BYTE out[8] = {};
    const DWORD written = Audio_DecodeMsAdpcm(&format, block, sizeof(block), out, sizeof(out));
    Check(written == 8, "one block decodes");
    Check(out[0] == 0x04 && out[1] == 0x03 && out[2] == 0x02 && out[3] == 0x01, "the decoder output is LE");
}

} // namespace

int main()
{
    LittleEndianSourceAndUnityGain();
    OneShotEndsAndRewinds();
    LoopWrapsAtTheRightFrame();
    ResamplingAdvancesAtTheVoiceRate();
    DownsamplingFiltersNyquist();
    SilentVoicesKeepPosition();
    PanFollowsTheDirectSoundLaw();
    VolumeChangesRampWithoutSteps();
    StoppingDoesNotClick();
    OverloadIsLimitedNotClipped();
    PausedHoldsPositions();
    Eight8BitAndStereo();
    DecoderWritesLittleEndian();
    if (failures == 0)
        std::printf("audio_mixer: ok\n");
    return failures != 0;
}
