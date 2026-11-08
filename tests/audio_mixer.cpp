// Fixed-point mixer behind the Amiga audio backend. The target compiles the
// same mixer.cpp and msadpcm.cpp, so the rules the game relies on are proved here.
#include "audio/mixer.h"
#include "port/audio.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
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


// ---- streams (movie sound) ------------------------------------------------

void Queue(MixStream *stream, const std::vector<float> &samples)
{
    const uint32_t frames = (uint32_t)(samples.size() / stream->channels);
    const uint32_t wrote = MixStream_WriteFloat(stream, samples.data(), frames, MixStream_Free(stream));
    MixStream_Commit(stream, wrote);
}

void StreamConvertsRoundsAndClamps()
{
    std::vector<int16_t> ring(8 * 2);
    MixStream stream;
    MixStream_Init(&stream, ring.data(), 8, 2, 1000);
    const float in[] = { 0.5f, -0.5f, 2.0f, -2.0f, NAN, 1.0f / 65534.0f };
    Check(MixStream_WriteFloat(&stream, in, 3, 8) == 3, "three stereo frames convert");
    Check(ring[0] == 16384 && ring[1] == -16384, "halves round away from zero");
    Check(ring[2] == 32767 && ring[3] == -32767, "out of range clamps");
    Check(ring[4] == 0 && ring[5] == 1, "NaN is silence, a half step rounds up");
    Check(stream.written == 0, "converting does not publish");
    Check(MixStream_WriteFloat(&stream, in, 3, 2) == 2, "the room limits the write");
}

void StreamWrapsAroundTheRing()
{
    Mixer_Init(&mixer, 1000);
    std::vector<int16_t> ring(8);
    MixStream stream;
    MixStream_Init(&stream, ring.data(), 8, 1, 1000);
    Mixer_PlayStream(&mixer, &stream);
    std::vector<float> first, second;
    for (int i = 0; i < 6; ++i)
        first.push_back((float)(i + 1) * 1000.0f / 32767.0f);
    for (int i = 0; i < 6; ++i)
        second.push_back((float)(i + 7) * 1000.0f / 32767.0f);
    Queue(&stream, first);
    int16_t out[6 * 2];
    Mixer_Render(&mixer, out, 5);
    Check(abs(out[0] - 1000) <= 1 && abs(out[8] - 5000) <= 1, "stream plays its frames in order");
    Check(MixStream_Free(&stream) == 7, "played frames free their room");
    Queue(&stream, second);
    Check(stream.written == 12, "the second write wraps the ring");
    Mixer_Render(&mixer, out, 6);
    bool ordered = true;
    for (int i = 0; i < 6; ++i)
        ordered = ordered && abs(out[i * 2] - (i + 6) * 1000) <= 1 && out[i * 2] == out[i * 2 + 1];
    Check(ordered, "frames across the wrap play in order, mono to both sides");
    Check(stream.underruns == 0, "no underrun while data is ahead");
}

void StreamUnderrunIsSilentAndResumes()
{
    Mixer_Init(&mixer, 1000);
    std::vector<int16_t> ring(64);
    MixStream stream;
    MixStream_Init(&stream, ring.data(), 64, 1, 1000);
    Mixer_PlayStream(&mixer, &stream);
    Queue(&stream, std::vector<float>(4, 0.25f));
    int16_t out[400 * 2];
    Mixer_Render(&mixer, out, 400);
    Check(stream.underruns == 1 && stream.starved, "running dry counts one underrun");
    Check(stream.consumed == stream.written, "the mixer never passes the writer");
    Check(abs(out[3 * 2] - 8192) <= 1 && out[4 * 2] < 8192 && out[399 * 2] < 100,
          "the dry stream fades from its last frame");
    Mixer_Render(&mixer, out, 400);
    Check(out[399 * 2] == 0 && mixer.declickLeft == 0, "and reaches silence");
    Check(stream.underruns == 1, "a long gap is one underrun");
    Queue(&stream, std::vector<float>(10, -0.5f));
    Mixer_Render(&mixer, out, 5);
    Check(!stream.starved && out[4 * 2] < -16000, "the stream resumes when data arrives");
    Check(MixStream_Free(&stream) == 64 - 5, "free room follows the play position");
}

// A 440 Hz sine fed in packets must come out without steps at the output rate.
void StreamResamplesContinuously(uint32_t sourceRate)
{
    Mixer_Init(&mixer, 48000);
    std::vector<int16_t> ring(4096 * 2);
    MixStream stream;
    MixStream_Init(&stream, ring.data(), 4096, 2, sourceRate);
    Mixer_PlayStream(&mixer, &stream);
    const double amplitude = 10000.0 / 32767.0;
    uint32_t fed = 0;
    int16_t out[480 * 2];
    int previous = 0, maxStep = 0, peak = 0;
    bool first = true;
    for (int block = 0; block < 100; ++block) {
        while (MixStream_Free(&stream) > 1500) {
            std::vector<float> packet;
            for (int i = 0; i < 700; ++i, ++fed) {
                const float v = (float)(amplitude * std::sin(2.0 * 3.14159265358979323846 * 440.0 * fed / sourceRate));
                packet.push_back(v);
                packet.push_back(v);
            }
            Queue(&stream, packet);
        }
        Mixer_Render(&mixer, out, 480);
        for (int i = 0; i < 480; ++i) {
            const int v = out[i * 2];
            if (!first)
                maxStep = abs(v - previous) > maxStep ? abs(v - previous) : maxStep;
            peak = abs(v) > peak ? abs(v) : peak;
            previous = v;
            first = false;
        }
    }
    const uint32_t expected = (uint32_t)((uint64_t)100 * 480 * sourceRate / 48000);
    char what[96];
    std::snprintf(what, sizeof(what), "%u Hz stream has no steps", (unsigned)sourceRate);
    Check(maxStep < 650 && peak > 9900 && peak < 10100, what);
    std::snprintf(what, sizeof(what), "%u Hz stream advances at its rate", (unsigned)sourceRate);
    Check(stream.underruns == 0 && stream.consumed + 1 >= expected && stream.consumed <= expected + 1, what);
}

void StreamStopsWithoutClick()
{
    Mixer_Init(&mixer, 22050);
    std::vector<int16_t> ring(1024 * 2);
    MixStream stream;
    MixStream_Init(&stream, ring.data(), 1024, 2, 22050);
    Mixer_PlayStream(&mixer, &stream);
    Queue(&stream, std::vector<float>(1024 * 2, 0.6f));
    int16_t out[1000 * 2];
    Mixer_Render(&mixer, out, 10);
    Mixer_StopStream(&mixer, &stream);
    Check(!stream.playing && mixer.streams == NULL, "a stopped stream leaves the mixer");
    Mixer_Render(&mixer, out, 1000);
    Check(out[0] > 18000 && out[1] > 18000, "a stopped stream decays from where it stopped");
    Check(out[1998] == 0 && mixer.declickLeft == 0 && mixer.declickRight == 0, "and reaches silence");
    Check(stream.consumed == 10, "nothing plays after the stop");
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
    StreamConvertsRoundsAndClamps();
    StreamWrapsAroundTheRing();
    StreamUnderrunIsSilentAndResumes();
    StreamResamplesContinuously(44100);
    StreamResamplesContinuously(22050);
    StreamStopsWithoutClick();
    if (failures == 0)
        std::printf("audio_mixer: ok\n");
    return failures != 0;
}
