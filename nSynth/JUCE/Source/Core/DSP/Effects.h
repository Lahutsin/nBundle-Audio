#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Params/Enums.h"

namespace nsynth
{
// Dual-mode BBD-style stereo chorus: I / II / I+II.
class StereoChorus
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int start, int num, ChorusMode mode) noexcept;

    struct ModeSpec { double rateHz, minDelayMs, maxDelayMs; };
    static ModeSpec specFor (ChorusMode mode) noexcept;

private:
    float readDelay (int channel, double delaySamples) const noexcept;

    double sr { 44100.0 };
    juce::AudioBuffer<float> line;
    int writePos { 0 };
    double phase { 0.0 };
    float wetGain { 0.0f };
    float lpState[2] {};
    float lpCoef { 0.5f };
};

// Stereo delay with send, feedback (1.0 = infinite, no degradation) and freeze.
class StereoDelay
{
public:
    static constexpr double kMaxSeconds = 6.0;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    // timeSeconds: target delay time; send 0..1; feedback 0..1
    void process (juce::AudioBuffer<float>& buffer, int start, int num,
                  double timeSeconds, float send, float feedback, bool freeze) noexcept;

    static double faderToSeconds (float fader) noexcept { return 0.001 * std::pow (1000.0, (double) juce::jlimit (0.0f, 1.0f, fader)); }

private:
    double sr { 44100.0 };
    juce::AudioBuffer<float> line;
    int writePos { 0 };
    double currentDelaySamples { -1.0 };
    float smoothedSend { 0.0f };
};
} // namespace nsynth
