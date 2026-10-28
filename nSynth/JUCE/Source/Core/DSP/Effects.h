#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include "../Params/Enums.h"

namespace nsynth
{
// Send-style MIX law shared by delay and reverb: up to 50 % the wet signal fades in
// under the full dry signal, above 50 % the dry signal fades out.
inline float dryGainForMix (float mix) noexcept { return juce::jlimit (0.0f, 1.0f, 2.0f * (1.0f - mix)); }
inline float wetGainForMix (float mix) noexcept { return juce::jlimit (0.0f, 1.0f, 2.0f * mix); }

// Stereo chorus: I / II / I+II run 1 / 2 / 3 modulated voices per side.
class StereoChorus
{
public:
    struct Settings
    {
        ChorusMode mode { ChorusMode::off };
        float rateHz { 0.5f };
        float depth { 0.5f };      // 0..1 of the centre delay
        float delayMs { 3.5f };    // centre delay
        float feedback { 0.0f };   // 0..0.9
        float width { 1.0f };      // 0 = same LFO phase left/right, 1 = opposite
        float mix { 0.5f };        // linear dry/wet crossfade
    };

    static constexpr double kMaxDelayMs = 40.0;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& settings) noexcept;

    static int voicesFor (ChorusMode mode) noexcept;

private:
    float readDelay (int channel, double delaySamples) const noexcept;
    static double lfoShape (double phase) noexcept;

    double sr { 44100.0 };
    juce::AudioBuffer<float> line;
    int writePos { 0 };
    double phase { 0.0 };
    float active { 0.0f };            // on/off fade
    float smoothedCentre { -1.0f }, smoothedDepth { -1.0f };
    float lpState[2] {}, feedbackState[2] {};
    float lpCoef { 0.5f };
};

// Stereo delay: tempo-synced or free time, filtered feedback path, ping-pong and freeze.
// Feedback 1.0 (or FREEZE) repeats forever without decay or degradation.
class StereoDelay
{
public:
    static constexpr double kMaxSeconds = 4.0;

    struct Settings
    {
        double timeSeconds { 0.35 };
        float feedback { 0.35f };
        float mix { 0.0f };
        bool pingPong { false };
        bool freeze { false };
        float lowCutHz { 20.0f };
        float highCutHz { 20000.0f };
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& settings) noexcept;

private:
    double sr { 44100.0 };
    juce::AudioBuffer<float> line;
    int writePos { 0 };
    double currentDelaySamples { -1.0 };
    float smoothedMix { -1.0f };
    float lowState[2] {}, highState[2] {};
};

// Algorithmic stereo reverb: pre-delay, input diffusion and an 8-line feedback delay
// network (Householder mixing) with per-line damping and slow modulation.
class StereoReverb
{
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;
    static constexpr double kMaxPreDelayMs = 260.0;

    struct Settings
    {
        float mix { 0.0f };
        float size { 0.6f };           // 0..1
        float decaySeconds { 2.5f };   // RT60
        float preDelayMs { 15.0f };
        float dampingHz { 9000.0f };
        float lowCutHz { 150.0f };
        float width { 1.0f };
        float modulation { 0.3f };     // 0..1
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& settings) noexcept;

    bool isIdle() const noexcept { return idle; }
    static double lineSeconds (int index, float size) noexcept;

private:
    struct Allpass
    {
        std::vector<float> buffer;
        int pos { 0 };
        float process (float x, float g) noexcept
        {
            const float delayed = buffer[(size_t) pos];
            const float v = x + g * delayed;
            buffer[(size_t) pos] = v;
            pos = pos + 1 < (int) buffer.size() ? pos + 1 : 0;
            return delayed - g * v;
        }
    };

    float readLine (int index, double delaySamples) const noexcept;

    double sr { 44100.0 };
    bool idle { true };
    std::array<std::vector<float>, 2> preDelay;
    int preDelayWrite { 0 };
    std::array<std::array<Allpass, kDiffusers>, 2> diffusers;
    std::array<std::vector<float>, kLines> lines;
    int lineWrite { 0 };
    std::array<float, kLines> dampState {};
    std::array<float, kLines> modCurrent {}, modStep {};
    std::array<double, kLines> modPhase {}, lineBaseSamples {};
    int lineSize { 1 };
    float lowCutState[2] {};
    int modCounter { 0 };
    float smoothedMix { -1.0f }, smoothedSize { -1.0f }, smoothedPreDelay { -1.0f };
};

// Stereo-linked safety limiter for the main output. Transparent below the ceiling; above it the
// gain drops instantly and recovers smoothly, so stacked chords never hard-clip the host bus.
class OutputLimiter
{
public:
    static constexpr float kCeiling = 0.966f;       // -0.3 dBFS
    static constexpr double kReleaseSeconds = 0.12;

    void prepare (double sampleRate);
    void reset() noexcept { gain = 1.0f; }
    void process (float* left, float* right, int num) noexcept;
    float getGain() const noexcept { return gain; }

private:
    float gain { 1.0f }, releaseCoef { 0.0f };
};

// Fixed gentle high-shelf ("air") that gives the layer output a brighter, more open top end.
class AirShelf
{
public:
    static constexpr float kFrequencyHz = 9000.0f;
    static constexpr float kGainDb = 1.5f;

    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int start, int num) noexcept;

private:
    float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f }, a1 { 0.0f }, a2 { 0.0f };
    float z1[2] {}, z2[2] {};
};
} // namespace nsynth
