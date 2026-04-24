#pragma once

#include <array>
#include <cstddef>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Generated/NEQCoefficientLut.h"
#include "Parameters.h"

namespace NEQ
{
inline constexpr int kLinearPhaseKernelSize = 129;
inline constexpr int kLinearPhaseLatencySamples = (kLinearPhaseKernelSize - 1) / 2;

class Processor
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, juce::AudioProcessorValueTreeState& state) noexcept;

private:
    struct AxisInterpolation
    {
        int lowerIndex { 0 };
        int upperIndex { 0 };
        float amount { 0.0f };
    };

    struct LookupState
    {
        bool enabled { true };
        bool hfEnabled { true };
        bool hmfEnabled { true };
        bool lmfEnabled { true };
        bool lfEnabled { true };
        int blackMode { 0 };
        int hfBell { 0 };
        int lfBell { 0 };
        AxisInterpolation sampleRate;
        AxisInterpolation hfGain;
        AxisInterpolation hfFreq;
        AxisInterpolation hmfGain;
        AxisInterpolation hmfFreq;
        AxisInterpolation hmfQ;
        AxisInterpolation lmfGain;
        AxisInterpolation lmfFreq;
        AxisInterpolation lmfQ;
        AxisInterpolation lfGain;
        AxisInterpolation lfFreq;
    };

    struct Parameters
    {
        int modelIndex { NEQ::params::kEqModelDefault };
        bool blackMode { false };
        bool hfEnabled { true };
        bool hmfEnabled { true };
        bool lmfEnabled { true };
        bool lfEnabled { true };
        bool hfBell { false };
        bool lfBell { false };
        float hfGainDb { 0.0f };
        float hfFreqHz { 5000.0f };
        float hmfGainDb { 0.0f };
        float hmfFreqHz { 1500.0f };
        float hmfQ { 1.0f };
        float lmfGainDb { 0.0f };
        float lmfFreqHz { 700.0f };
        float lmfQ { 1.0f };
        float lfGainDb { 0.0f };
        float lfFreqHz { 60.0f };
    };

    struct ProcessParameters
    {
        bool enabled { true };
        bool midSideMode { false };
        int modelIndex { NEQ::params::kEqModelDefault };
        float stereoWidth { 1.0f };
        float inputGainDb { 0.0f };
        float outputGainDb { 0.0f };
        std::array<Parameters, NEQ::params::pathCount> path;
    };

    struct PathState
    {
        std::array<float, kLinearPhaseKernelSize> firKernel {};
        std::array<float, kLinearPhaseKernelSize * 2> firDelayLine {};
        size_t firWriteIndex { 0 };
        juce::LinearSmoothedValue<float> outputGain;
        double dryPowerAverage { 1.0 };
        double wetPowerAverage { 1.0 };
        float staticAutoGainDb { 0.0f };
        float adaptiveAutoGainDb { 0.0f };
        LookupState activeLookupState;
        bool lookupStateValid { false };
    };

    static float getFloatParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback) noexcept;
    static bool getBoolParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, bool fallback) noexcept;
    static int getChoiceParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, int fallback, int maxExclusive) noexcept;
    template <size_t N>
    static AxisInterpolation interpolationForValue(const std::array<float, N>& values, float target) noexcept;
    static bool isSameLookupState(const LookupState& a, const LookupState& b) noexcept;
    static bool isSameAxisInterpolation(const AxisInterpolation& a, const AxisInterpolation& b) noexcept;
    static void initialiseLinearPhaseKernel(PathState& pathState) noexcept;
    static Parameters applyModelVoicing(Parameters parameters) noexcept;
    static float estimateAnalogueDrive(const Parameters& parameters) noexcept;
    static float estimatePreStageAsymmetry(const Parameters& parameters) noexcept;
    static float estimatePostStageAsymmetry(const Parameters& parameters) noexcept;
    static float processAnalogueStageSample(float sample, float drive, float asymmetry, int modelIndex) noexcept;
    static float processEqSample(PathState& pathState, float sample) noexcept;
    static double magnitudeForBiquad(const coeff::Biquad& biquad, double frequency, double sampleRate) noexcept;
    static coeff::Biquad interpolateBiquads(const coeff::Biquad& a, const coeff::Biquad& b, float amount) noexcept;
    static float estimateAutoGainDb(const LookupState& state, double sampleRate) noexcept;
    static const coeff::Biquad& lookupHfDiscrete(int sampleRateIndex, int blackMode, int hfBell, int gainIndex, int freqIndex) noexcept;
    static const coeff::Biquad& lookupHmfDiscrete(int sampleRateIndex, int blackMode, int gainIndex, int freqIndex, int qIndex) noexcept;
    static const coeff::Biquad& lookupLmfDiscrete(int sampleRateIndex, int blackMode, int gainIndex, int freqIndex, int qIndex) noexcept;
    static const coeff::Biquad& lookupLfDiscrete(int sampleRateIndex, int blackMode, int lfBell, int gainIndex, int freqIndex) noexcept;
    static coeff::Biquad lookupHf(const LookupState& state) noexcept;
    static coeff::Biquad lookupHmf(const LookupState& state) noexcept;
    static coeff::Biquad lookupLmf(const LookupState& state) noexcept;
    static coeff::Biquad lookupLf(const LookupState& state) noexcept;

    Parameters readPathParameters(juce::AudioProcessorValueTreeState& state, int pathIndex, int modelIndex) const noexcept;
    ProcessParameters readParameters(juce::AudioProcessorValueTreeState& state) const noexcept;
    LookupState makeLookupState(const Parameters& parameters) const noexcept;
    void updateCoefficients(PathState& pathState, const LookupState& state) noexcept;
    void updateAdaptiveAutoGain(PathState& pathState, double dryBlockPower, double wetBlockPower, int numSamples, bool enabled) noexcept;
    void updateOutputGainTarget(PathState& pathState, bool enabled) noexcept;

    double currentSampleRate { 44100.0 };
    std::array<PathState, NEQ::params::pathCount> pathStates;
};
} // namespace NEQ