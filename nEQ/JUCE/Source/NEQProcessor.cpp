#include "NEQProcessor.h"

#include <complex>
#include <cmath>
#include "Parameters.h"

namespace NEQ
{
namespace
{
constexpr int kAutoGainProbeCount = 513;
constexpr double kAutoGainPowerFloor = 1.0e-9;
constexpr double kAdaptiveAutoGainTimeSeconds = 0.35;
constexpr float kAdaptiveAutoGainLimitDb = 6.0f;
constexpr float kMidSideNormaliser = 0.70710678118f;
constexpr int kLinearPhaseDesignFftSize = 1024;

float modelEffectStrength(int modelIndex) noexcept
{
    switch (modelIndex)
    {
        case params::modelB:
            return 0.80f;
        case params::modelC:
            return 0.95f;
        case params::modelF:
            return 0.70f;
        default:
            return 1.0f;
    }
}

float scaleFromUnity(float value, float strength) noexcept
{
    return 1.0f + (value - 1.0f) * strength;
}

float scaleOffset(float value, float strength) noexcept
{
    return value * strength;
}

double aWeightingMagnitudeSquared(double frequency) noexcept
{
    const auto f2 = frequency * frequency;
    const auto numerator = std::pow(12200.0, 2.0) * f2 * f2;
    const auto denominator = (f2 + std::pow(20.6, 2.0))
                           * std::sqrt((f2 + std::pow(107.7, 2.0)) * (f2 + std::pow(737.9, 2.0)))
                           * (f2 + std::pow(12200.0, 2.0));

    if (denominator <= 1.0e-18)
        return 0.0;

    constexpr double kAWeightingNormalisation = 1.2588966;
    const auto magnitude = kAWeightingNormalisation * numerator / denominator;
    return magnitude * magnitude;
}

float blackmanWindow(int index, int size) noexcept
{
    if (size <= 1)
        return 1.0f;

    const auto proportion = static_cast<double>(index) / static_cast<double>(size - 1);
    const auto angle = 2.0 * juce::MathConstants<double>::pi * proportion;
    return static_cast<float>(0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle));
}

coeff::Biquad identityBiquad() noexcept
{
    return { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
}
}

void Processor::prepare(const juce::dsp::ProcessSpec& spec)
{
    currentSampleRate = spec.sampleRate;

    for (auto& pathState : pathStates)
    {
        pathState.outputGain.reset(spec.sampleRate, 0.05);
        pathState.outputGain.setCurrentAndTargetValue(1.0f);
        pathState.dryPowerAverage = 1.0;
        pathState.wetPowerAverage = 1.0;
        pathState.staticAutoGainDb = 0.0f;
        pathState.adaptiveAutoGainDb = 0.0f;
        initialiseLinearPhaseKernel(pathState);
        pathState.lookupStateValid = false;
    }
}

void Processor::reset() noexcept
{
    for (auto& pathState : pathStates)
    {
        pathState.firDelayLine.fill(0.0f);
        pathState.firWriteIndex = 0;
        pathState.outputGain.setCurrentAndTargetValue(pathState.outputGain.getTargetValue());
        pathState.dryPowerAverage = 1.0;
        pathState.wetPowerAverage = 1.0;
        pathState.adaptiveAutoGainDb = 0.0f;
    }
}

float Processor::getFloatParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback) noexcept
{
    if (auto* value = state.getRawParameterValue(id))
        return value->load();

    return fallback;
}

bool Processor::getBoolParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, bool fallback) noexcept
{
    return getFloatParam(state, id, fallback ? 1.0f : 0.0f) >= 0.5f;
}

int Processor::getChoiceParam(juce::AudioProcessorValueTreeState& state, const juce::String& id, int fallback, int maxExclusive) noexcept
{
    return juce::jlimit(0, juce::jmax(0, maxExclusive - 1), juce::roundToInt(getFloatParam(state, id, static_cast<float>(fallback))));
}

template <size_t N>
Processor::AxisInterpolation Processor::interpolationForValue(const std::array<float, N>& values, float target) noexcept
{
    target = juce::jlimit(values.front(), values.back(), target);

    if (target <= values.front())
        return { 0, 0, 0.0f };

    if (target >= values.back())
        return { static_cast<int>(N) - 1, static_cast<int>(N) - 1, 0.0f };

    for (size_t index = 1; index < N; ++index)
    {
        if (target <= values[index])
        {
            const auto lower = static_cast<int>(index - 1);
            const auto upper = static_cast<int>(index);
            const auto span = values[index] - values[index - 1];
            const auto amount = span > 0.0f ? (target - values[index - 1]) / span : 0.0f;
            return { lower, upper, juce::jlimit(0.0f, 1.0f, amount) };
        }
    }

    return { static_cast<int>(N) - 1, static_cast<int>(N) - 1, 0.0f };
}

bool Processor::isSameLookupState(const LookupState& a, const LookupState& b) noexcept
{
    return a.enabled == b.enabled
        && a.hfEnabled == b.hfEnabled
        && a.hmfEnabled == b.hmfEnabled
        && a.lmfEnabled == b.lmfEnabled
        && a.lfEnabled == b.lfEnabled
        && a.blackMode == b.blackMode
        && a.hfBell == b.hfBell
        && a.lfBell == b.lfBell
        && isSameAxisInterpolation(a.sampleRate, b.sampleRate)
        && isSameAxisInterpolation(a.hfGain, b.hfGain)
        && isSameAxisInterpolation(a.hfFreq, b.hfFreq)
        && isSameAxisInterpolation(a.hmfGain, b.hmfGain)
        && isSameAxisInterpolation(a.hmfFreq, b.hmfFreq)
        && isSameAxisInterpolation(a.hmfQ, b.hmfQ)
        && isSameAxisInterpolation(a.lmfGain, b.lmfGain)
        && isSameAxisInterpolation(a.lmfFreq, b.lmfFreq)
        && isSameAxisInterpolation(a.lmfQ, b.lmfQ)
        && isSameAxisInterpolation(a.lfGain, b.lfGain)
        && isSameAxisInterpolation(a.lfFreq, b.lfFreq);
}

bool Processor::isSameAxisInterpolation(const AxisInterpolation& a, const AxisInterpolation& b) noexcept
{
    return a.lowerIndex == b.lowerIndex
        && a.upperIndex == b.upperIndex
        && std::abs(a.amount - b.amount) < 1.0e-6f;
}

void Processor::initialiseLinearPhaseKernel(PathState& pathState) noexcept
{
    pathState.firKernel.fill(0.0f);
    pathState.firDelayLine.fill(0.0f);
    pathState.firKernel[static_cast<size_t>(kLinearPhaseLatencySamples)] = 1.0f;
    pathState.firWriteIndex = 0;
}

Processor::Parameters Processor::applyModelVoicing(Parameters parameters) noexcept
{
    const auto strength = modelEffectStrength(parameters.modelIndex);

    switch (parameters.modelIndex)
    {
        case params::modelB:
            parameters.hfGainDb *= scaleFromUnity(1.18f, strength);
            parameters.hmfGainDb *= scaleFromUnity(1.34f, strength);
            parameters.lmfGainDb *= scaleFromUnity(1.32f, strength);
            parameters.lfGainDb *= scaleFromUnity(1.14f, strength);
            parameters.hfFreqHz *= scaleFromUnity(1.10f, strength);
            parameters.hmfFreqHz *= scaleFromUnity(1.14f, strength);
            parameters.lmfFreqHz *= scaleFromUnity(1.10f, strength);
            parameters.hmfQ *= scaleFromUnity(1.18f + std::abs(parameters.hmfGainDb) * 0.055f, strength);
            parameters.lmfQ *= scaleFromUnity(1.15f + std::abs(parameters.lmfGainDb) * 0.05f, strength);
            break;

        case params::modelC:
            parameters.hfFreqHz *= scaleFromUnity(0.82f, strength);
            parameters.hmfFreqHz *= scaleFromUnity(0.86f, strength);
            parameters.lmfFreqHz *= scaleFromUnity(0.84f, strength);
            parameters.lfFreqHz *= scaleFromUnity(0.78f, strength);
            parameters.hmfQ *= scaleFromUnity(0.66f, strength);
            parameters.lmfQ *= scaleFromUnity(0.62f, strength);
            parameters.hfGainDb *= scaleFromUnity(0.92f, strength);
            parameters.hmfGainDb *= scaleFromUnity(1.08f, strength);
            parameters.lmfGainDb *= scaleFromUnity(1.10f, strength);
            parameters.lfGainDb *= scaleFromUnity(1.18f, strength);
            break;

        case params::modelD:
            parameters.hfBell = false;
            parameters.hfFreqHz *= 1.80f;
            parameters.hfGainDb *= 1.30f;
            parameters.hmfGainDb *= 0.78f;
            parameters.lmfGainDb *= 0.76f;
            parameters.hmfQ *= 0.58f;
            parameters.lmfQ *= 0.55f;
            parameters.lfGainDb *= 0.84f;
            parameters.lfFreqHz *= 1.16f;
            break;

        case params::modelF:
            parameters.hfFreqHz *= scaleFromUnity(0.96f, strength);
            parameters.hmfFreqHz *= scaleFromUnity(0.93f, strength);
            parameters.lmfFreqHz *= scaleFromUnity(0.92f, strength);
            parameters.lfFreqHz *= scaleFromUnity(0.88f, strength);
            parameters.hfGainDb *= scaleFromUnity(1.06f, strength);
            parameters.hmfGainDb *= scaleFromUnity(1.16f, strength);
            parameters.lmfGainDb *= scaleFromUnity(1.20f, strength);
            parameters.lfGainDb *= scaleFromUnity(1.10f, strength);
            parameters.hmfQ *= scaleFromUnity(0.88f, strength);
            parameters.lmfQ *= scaleFromUnity(0.84f, strength);
            parameters.hfBell = true;
            parameters.lfBell = true;
            break;

        case params::modelA:
        default:
            parameters.hfGainDb *= 1.02f;
            parameters.hmfGainDb *= 1.04f;
            parameters.lmfGainDb *= 1.04f;
            parameters.lfGainDb *= 1.02f;
            break;
    }

    parameters.hfFreqHz = juce::jlimit(coeff::hfFrequencies.front(), coeff::hfFrequencies.back(), parameters.hfFreqHz);
    parameters.hmfFreqHz = juce::jlimit(coeff::hmfFrequencies.front(), coeff::hmfFrequencies.back(), parameters.hmfFreqHz);
    parameters.lmfFreqHz = juce::jlimit(coeff::lmfFrequencies.front(), coeff::lmfFrequencies.back(), parameters.lmfFreqHz);
    parameters.lfFreqHz = juce::jlimit(coeff::lfFrequencies.front(), coeff::lfFrequencies.back(), parameters.lfFreqHz);
    parameters.hmfQ = juce::jlimit(coeff::qValues.front(), coeff::qValues.back(), parameters.hmfQ);
    parameters.lmfQ = juce::jlimit(coeff::qValues.front(), coeff::qValues.back(), parameters.lmfQ);
    return parameters;
}

float Processor::estimateAnalogueDrive(const Parameters& parameters) noexcept
{
    const auto weightedGainActivity = (parameters.hfEnabled ? std::abs(parameters.hfGainDb) * 0.55f : 0.0f)
                                    + (parameters.hmfEnabled ? std::abs(parameters.hmfGainDb) * 0.85f : 0.0f)
                                    + (parameters.lmfEnabled ? std::abs(parameters.lmfGainDb) * 0.85f : 0.0f)
                                    + (parameters.lfEnabled ? std::abs(parameters.lfGainDb) * 0.55f : 0.0f);

    const auto normalised = weightedGainActivity / (parameters.blackMode ? 20.0f : 26.0f);
    const auto baseDrive = juce::jlimit(0.0f, 1.0f, normalised);
    auto drive = baseDrive * (parameters.blackMode ? 1.18f : 0.96f);
    const auto strength = modelEffectStrength(parameters.modelIndex);

    switch (parameters.modelIndex)
    {
        case params::modelB:
            drive *= scaleFromUnity(1.26f, strength);
            break;
        case params::modelC:
            drive *= scaleFromUnity(1.52f, strength);
            break;
        case params::modelD:
            drive *= 0.68f;
            break;
        case params::modelF:
            drive *= scaleFromUnity(1.12f, strength);
            break;
        case params::modelA:
        default:
            drive *= 0.98f;
            break;
    }

    return juce::jlimit(0.0f, 1.2f, drive);
}

float Processor::estimatePreStageAsymmetry(const Parameters& parameters) noexcept
{
    auto asymmetry = parameters.blackMode ? 0.75f : 0.45f;
    const auto strength = modelEffectStrength(parameters.modelIndex);

    switch (parameters.modelIndex)
    {
        case params::modelB:
            asymmetry *= scaleFromUnity(1.18f, strength);
            break;
        case params::modelC:
            asymmetry *= scaleFromUnity(1.42f, strength);
            break;
        case params::modelD:
            asymmetry *= 0.62f;
            break;
        case params::modelF:
            asymmetry *= scaleFromUnity(0.92f, strength);
            break;
        case params::modelA:
        default:
            asymmetry *= 0.96f;
            break;
    }

    return asymmetry;
}

float Processor::estimatePostStageAsymmetry(const Parameters& parameters) noexcept
{
    auto asymmetry = parameters.blackMode ? 1.0f : 0.7f;
    const auto strength = modelEffectStrength(parameters.modelIndex);

    switch (parameters.modelIndex)
    {
        case params::modelB:
            asymmetry *= scaleFromUnity(1.24f, strength);
            break;
        case params::modelC:
            asymmetry *= scaleFromUnity(1.60f, strength);
            break;
        case params::modelD:
            asymmetry *= 0.66f;
            break;
        case params::modelF:
            asymmetry *= scaleFromUnity(0.96f, strength);
            break;
        case params::modelA:
        default:
            asymmetry *= 0.98f;
            break;
    }

    return asymmetry;
}

float Processor::processAnalogueStageSample(float sample, float drive, float asymmetry, int modelIndex) noexcept
{
    if (drive <= 1.0e-4f)
        return sample;

    const auto inputTrim = 1.0f + drive * 1.45f;
    // Smooth sign-aware bias avoids DC at silence and reduces low-level ripple artifacts.
    const auto bias = asymmetry * drive * 0.06f * std::tanh(sample * 5.0f);
    const auto driven = (sample + bias) * inputTrim;
    const auto strength = modelEffectStrength(modelIndex);
    float shaped = driven;

    switch (modelIndex)
    {
        case params::modelB:
        {
            const auto apiOdd = std::tanh(driven * (1.10f + drive * 0.35f));
            const auto apiEdge = std::atan(driven * (1.35f + drive * 0.45f)) * 0.78f;
            shaped = juce::jmap(0.58f, apiOdd, apiEdge);
            break;
        }

        case params::modelC:
        {
            const auto neveCore = std::tanh(driven * (0.92f + drive * 0.28f));
            const auto evenBloom = std::tanh((driven + 0.16f * drive) * (0.76f + drive * 0.18f));
            shaped = juce::jmap(0.34f, neveCore, evenBloom) + scaleOffset(0.018f, strength) * drive;
            break;
        }

        case params::modelD:
        {
            const auto airySmooth = std::tanh(driven * (0.72f + drive * 0.16f));
            const auto softShelf = driven / (1.0f + std::abs(driven) * (0.52f + drive * 0.24f));
            shaped = juce::jmap(0.30f, airySmooth, softShelf);
            break;
        }

        case params::modelF:
        {
            const auto harrisonPunch = std::atan(driven * (1.18f + drive * 0.32f)) * 0.86f;
            const auto harrisonBite = std::tanh((driven - 0.07f * drive) * (1.02f + drive * 0.26f));
            shaped = juce::jmap(0.48f, harrisonPunch, harrisonBite);
            break;
        }

        case params::modelA:
        default:
        {
            shaped = std::tanh(driven);
            break;
        }
    }

    const auto compensated = shaped / (1.0f + drive * 0.42f);
    auto blend = 0.09f + drive * 0.19f;

    switch (modelIndex)
    {
        case params::modelB:
            blend += scaleOffset(0.025f, strength);
            break;
        case params::modelC:
            blend += scaleOffset(0.04f, strength);
            break;
        case params::modelD:
            blend -= 0.02f;
            break;
        case params::modelF:
            blend += scaleOffset(0.015f, strength);
            break;
        case params::modelA:
        default:
            break;
    }

    blend = juce::jlimit(0.05f, 0.42f, blend);
    return juce::jmap(blend, sample, compensated);
}

float Processor::processEqSample(PathState& pathState, float sample) noexcept
{
    if (pathState.firWriteIndex == 0)
        pathState.firWriteIndex = static_cast<size_t>(kLinearPhaseKernelSize - 1);
    else
        --pathState.firWriteIndex;

    const auto writeIndex = pathState.firWriteIndex;
    pathState.firDelayLine[writeIndex] = sample;
    pathState.firDelayLine[writeIndex + static_cast<size_t>(kLinearPhaseKernelSize)] = sample;

    const auto* history = pathState.firDelayLine.data() + writeIndex;
    constexpr size_t halfKernelSize = static_cast<size_t>(kLinearPhaseKernelSize / 2);
    auto output = pathState.firKernel[halfKernelSize] * history[halfKernelSize];

    for (size_t tapIndex = 0; tapIndex < halfKernelSize; ++tapIndex)
        output += pathState.firKernel[tapIndex] * (history[tapIndex] + history[static_cast<size_t>(kLinearPhaseKernelSize - 1) - tapIndex]);

    return output;
}

double Processor::magnitudeForBiquad(const coeff::Biquad& biquad, double frequency, double sampleRate) noexcept
{
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const std::complex<double> z1(std::cos(omega), -std::sin(omega));
    const auto z2 = z1 * z1;

    const auto numerator = static_cast<double>(biquad.b0) + static_cast<double>(biquad.b1) * z1 + static_cast<double>(biquad.b2) * z2;
    const auto denominator = static_cast<double>(biquad.a0) + static_cast<double>(biquad.a1) * z1 + static_cast<double>(biquad.a2) * z2;

    if (std::abs(denominator) <= 1.0e-12)
        return 1.0;

    return std::abs(numerator / denominator);
}

coeff::Biquad Processor::interpolateBiquads(const coeff::Biquad& a, const coeff::Biquad& b, float amount) noexcept
{
    if (amount <= 0.0f)
        return a;

    if (amount >= 1.0f)
        return b;

    return {
        juce::jmap(amount, a.b0, b.b0),
        juce::jmap(amount, a.b1, b.b1),
        juce::jmap(amount, a.b2, b.b2),
        juce::jmap(amount, a.a0, b.a0),
        juce::jmap(amount, a.a1, b.a1),
        juce::jmap(amount, a.a2, b.a2)
    };
}

const coeff::Biquad& Processor::lookupHfDiscrete(int sampleRateIndex, int blackMode, int hfBell, int gainIndex, int freqIndex) noexcept
{
    const auto index = ((((static_cast<size_t>(sampleRateIndex) * 2u) + static_cast<size_t>(blackMode)) * 2u + static_cast<size_t>(hfBell))
                      * coeff::kGainCount + static_cast<size_t>(gainIndex)) * coeff::kHfFreqCount
                      + static_cast<size_t>(freqIndex);
    return coeff::hfTable[index];
}

const coeff::Biquad& Processor::lookupHmfDiscrete(int sampleRateIndex, int blackMode, int gainIndex, int freqIndex, int qIndex) noexcept
{
    const auto index = ((((static_cast<size_t>(sampleRateIndex) * 2u) + static_cast<size_t>(blackMode))
                      * coeff::kGainCount + static_cast<size_t>(gainIndex)) * coeff::kMidFreqCount
                      + static_cast<size_t>(freqIndex)) * coeff::kQCount
                      + static_cast<size_t>(qIndex);
    return coeff::hmfTable[index];
}

const coeff::Biquad& Processor::lookupLmfDiscrete(int sampleRateIndex, int blackMode, int gainIndex, int freqIndex, int qIndex) noexcept
{
    const auto index = ((((static_cast<size_t>(sampleRateIndex) * 2u) + static_cast<size_t>(blackMode))
                      * coeff::kGainCount + static_cast<size_t>(gainIndex)) * coeff::kMidFreqCount
                      + static_cast<size_t>(freqIndex)) * coeff::kQCount
                      + static_cast<size_t>(qIndex);
    return coeff::lmfTable[index];
}

const coeff::Biquad& Processor::lookupLfDiscrete(int sampleRateIndex, int blackMode, int lfBell, int gainIndex, int freqIndex) noexcept
{
    const auto index = ((((static_cast<size_t>(sampleRateIndex) * 2u) + static_cast<size_t>(blackMode)) * 2u + static_cast<size_t>(lfBell))
                      * coeff::kGainCount + static_cast<size_t>(gainIndex)) * coeff::kLfFreqCount
                      + static_cast<size_t>(freqIndex);
    return coeff::lfTable[index];
}

coeff::Biquad Processor::lookupHf(const LookupState& state) noexcept
{
    if (!state.hfEnabled)
        return identityBiquad();

    const auto sampleLower = state.sampleRate.lowerIndex;
    const auto sampleUpper = state.sampleRate.upperIndex;

    const auto blendFrequency = [&] (int sampleRateIndex, int gainIndex)
    {
        return interpolateBiquads(lookupHfDiscrete(sampleRateIndex, state.blackMode, state.hfBell, gainIndex, state.hfFreq.lowerIndex),
                                  lookupHfDiscrete(sampleRateIndex, state.blackMode, state.hfBell, gainIndex, state.hfFreq.upperIndex),
                                  state.hfFreq.amount);
    };

    const auto blendGain = [&] (int sampleRateIndex)
    {
        return interpolateBiquads(blendFrequency(sampleRateIndex, state.hfGain.lowerIndex),
                                  blendFrequency(sampleRateIndex, state.hfGain.upperIndex),
                                  state.hfGain.amount);
    };

    return interpolateBiquads(blendGain(sampleLower), blendGain(sampleUpper), state.sampleRate.amount);
}

coeff::Biquad Processor::lookupHmf(const LookupState& state) noexcept
{
    if (!state.hmfEnabled)
        return identityBiquad();

    const auto sampleLower = state.sampleRate.lowerIndex;
    const auto sampleUpper = state.sampleRate.upperIndex;

    const auto blendQ = [&] (int sampleRateIndex, int gainIndex, int freqIndex)
    {
        return interpolateBiquads(lookupHmfDiscrete(sampleRateIndex, state.blackMode, gainIndex, freqIndex, state.hmfQ.lowerIndex),
                                  lookupHmfDiscrete(sampleRateIndex, state.blackMode, gainIndex, freqIndex, state.hmfQ.upperIndex),
                                  state.hmfQ.amount);
    };

    const auto blendFrequency = [&] (int sampleRateIndex, int gainIndex)
    {
        return interpolateBiquads(blendQ(sampleRateIndex, gainIndex, state.hmfFreq.lowerIndex),
                                  blendQ(sampleRateIndex, gainIndex, state.hmfFreq.upperIndex),
                                  state.hmfFreq.amount);
    };

    const auto blendGain = [&] (int sampleRateIndex)
    {
        return interpolateBiquads(blendFrequency(sampleRateIndex, state.hmfGain.lowerIndex),
                                  blendFrequency(sampleRateIndex, state.hmfGain.upperIndex),
                                  state.hmfGain.amount);
    };

    return interpolateBiquads(blendGain(sampleLower), blendGain(sampleUpper), state.sampleRate.amount);
}

coeff::Biquad Processor::lookupLmf(const LookupState& state) noexcept
{
    if (!state.lmfEnabled)
        return identityBiquad();

    const auto sampleLower = state.sampleRate.lowerIndex;
    const auto sampleUpper = state.sampleRate.upperIndex;

    const auto blendQ = [&] (int sampleRateIndex, int gainIndex, int freqIndex)
    {
        return interpolateBiquads(lookupLmfDiscrete(sampleRateIndex, state.blackMode, gainIndex, freqIndex, state.lmfQ.lowerIndex),
                                  lookupLmfDiscrete(sampleRateIndex, state.blackMode, gainIndex, freqIndex, state.lmfQ.upperIndex),
                                  state.lmfQ.amount);
    };

    const auto blendFrequency = [&] (int sampleRateIndex, int gainIndex)
    {
        return interpolateBiquads(blendQ(sampleRateIndex, gainIndex, state.lmfFreq.lowerIndex),
                                  blendQ(sampleRateIndex, gainIndex, state.lmfFreq.upperIndex),
                                  state.lmfFreq.amount);
    };

    const auto blendGain = [&] (int sampleRateIndex)
    {
        return interpolateBiquads(blendFrequency(sampleRateIndex, state.lmfGain.lowerIndex),
                                  blendFrequency(sampleRateIndex, state.lmfGain.upperIndex),
                                  state.lmfGain.amount);
    };

    return interpolateBiquads(blendGain(sampleLower), blendGain(sampleUpper), state.sampleRate.amount);
}

coeff::Biquad Processor::lookupLf(const LookupState& state) noexcept
{
    if (!state.lfEnabled)
        return identityBiquad();

    const auto sampleLower = state.sampleRate.lowerIndex;
    const auto sampleUpper = state.sampleRate.upperIndex;

    const auto blendFrequency = [&] (int sampleRateIndex, int gainIndex)
    {
        return interpolateBiquads(lookupLfDiscrete(sampleRateIndex, state.blackMode, state.lfBell, gainIndex, state.lfFreq.lowerIndex),
                                  lookupLfDiscrete(sampleRateIndex, state.blackMode, state.lfBell, gainIndex, state.lfFreq.upperIndex),
                                  state.lfFreq.amount);
    };

    const auto blendGain = [&] (int sampleRateIndex)
    {
        return interpolateBiquads(blendFrequency(sampleRateIndex, state.lfGain.lowerIndex),
                                  blendFrequency(sampleRateIndex, state.lfGain.upperIndex),
                                  state.lfGain.amount);
    };

    return interpolateBiquads(blendGain(sampleLower), blendGain(sampleUpper), state.sampleRate.amount);
}

float Processor::estimateAutoGainDb(const LookupState& state, double sampleRate) noexcept
{
    const auto& hfCoefficients = lookupHf(state);
    const auto& hmfCoefficients = lookupHmf(state);
    const auto& lmfCoefficients = lookupLmf(state);
    const auto& lfCoefficients = lookupLf(state);

    double weightedPowerSum = 0.0;
    double weightSum = 0.0;
    constexpr double startFrequency = 20.0;
    constexpr double endFrequency = 20000.0;
    const auto logStart = std::log(startFrequency);
    const auto logEnd = std::log(endFrequency);

    for (int probeIndex = 0; probeIndex < kAutoGainProbeCount; ++probeIndex)
    {
        const auto proportion = static_cast<double>(probeIndex) / static_cast<double>(kAutoGainProbeCount - 1);
        const auto frequency = std::exp(logStart + (logEnd - logStart) * proportion);
        const auto magnitude = magnitudeForBiquad(hfCoefficients, frequency, sampleRate)
                             * magnitudeForBiquad(hmfCoefficients, frequency, sampleRate)
                             * magnitudeForBiquad(lmfCoefficients, frequency, sampleRate)
                             * magnitudeForBiquad(lfCoefficients, frequency, sampleRate);
        const auto integrationWeight = (probeIndex == 0 || probeIndex == kAutoGainProbeCount - 1) ? 0.5 : 1.0;
        const auto perceptualWeight = 0.7 + 0.3 * aWeightingMagnitudeSquared(frequency);
        const auto totalWeight = integrationWeight * perceptualWeight;
        weightedPowerSum += totalWeight * magnitude * magnitude;
        weightSum += totalWeight;
    }

    const auto averagePower = weightSum > 0.0 ? weightedPowerSum / weightSum : 1.0;
    const auto compensationDb = averagePower > 0.0
        ? static_cast<float>(-10.0 * std::log10(averagePower))
        : 0.0f;

    return juce::jlimit(-12.0f, 12.0f, compensationDb);
}

Processor::Parameters Processor::readPathParameters(juce::AudioProcessorValueTreeState& state, int pathIndex, int modelIndex) const noexcept
{
    const auto ids = params::idsForPath(pathIndex);
    Parameters parameters;
    parameters.modelIndex = modelIndex;
    parameters.blackMode = getBoolParam(state, ids.blackMode, false);
    parameters.hfEnabled = getBoolParam(state, ids.hfEnable, true);
    parameters.hmfEnabled = getBoolParam(state, ids.hmfEnable, true);
    parameters.lmfEnabled = getBoolParam(state, ids.lmfEnable, true);
    parameters.lfEnabled = getBoolParam(state, ids.lfEnable, true);
    parameters.hfBell = getBoolParam(state, ids.hfBell, false);
    parameters.lfBell = getBoolParam(state, ids.lfBell, false);
    parameters.hfGainDb = getFloatParam(state, ids.hfGain, 0.0f);
    parameters.hfFreqHz = getFloatParam(state, ids.hfFreq, 5000.0f);
    parameters.hmfGainDb = getFloatParam(state, ids.hmfGain, 0.0f);
    parameters.hmfFreqHz = getFloatParam(state, ids.hmfFreq, 1500.0f);
    parameters.hmfQ = getFloatParam(state, ids.hmfQ, 1.0f);
    parameters.lmfGainDb = getFloatParam(state, ids.lmfGain, 0.0f);
    parameters.lmfFreqHz = getFloatParam(state, ids.lmfFreq, 700.0f);
    parameters.lmfQ = getFloatParam(state, ids.lmfQ, 1.0f);
    parameters.lfGainDb = getFloatParam(state, ids.lfGain, 0.0f);
    parameters.lfFreqHz = getFloatParam(state, ids.lfFreq, 60.0f);
    return applyModelVoicing(parameters);
}

Processor::ProcessParameters Processor::readParameters(juce::AudioProcessorValueTreeState& state) const noexcept
{
    ProcessParameters parameters;
    parameters.enabled = getBoolParam(state, params::eqIn, true);
    parameters.midSideMode = getBoolParam(state, params::msMode, false);
    parameters.modelIndex = getChoiceParam(state, params::eqModel, params::kEqModelDefault, params::kEqModelCount);
    parameters.stereoWidth = juce::jlimit(0.0f, 2.0f, getFloatParam(state, params::stereoWidth, 1.0f));
    parameters.inputGainDb = juce::jlimit(-12.0f, 12.0f, getFloatParam(state, params::inputGain, 0.0f));
    parameters.outputGainDb = juce::jlimit(-12.0f, 12.0f, getFloatParam(state, params::outputGain, 0.0f));

    for (int pathIndex = 0; pathIndex < params::pathCount; ++pathIndex)
        parameters.path[static_cast<size_t>(pathIndex)] = readPathParameters(state, pathIndex, parameters.modelIndex);

    return parameters;
}

Processor::LookupState Processor::makeLookupState(const Parameters& parameters) const noexcept
{
    LookupState state;
    state.enabled = true;
    state.hfEnabled = parameters.hfEnabled;
    state.hmfEnabled = parameters.hmfEnabled;
    state.lmfEnabled = parameters.lmfEnabled;
    state.lfEnabled = parameters.lfEnabled;
    state.sampleRate = interpolationForValue(coeff::sampleRates, static_cast<float>(currentSampleRate));
    state.blackMode = parameters.blackMode ? 1 : 0;
    state.hfBell = parameters.hfBell ? 1 : 0;
    state.lfBell = parameters.lfBell ? 1 : 0;
    state.hfGain = interpolationForValue(coeff::gainValues, juce::jlimit(coeff::gainValues.front(), coeff::gainValues.back(), parameters.hfGainDb));
    state.hfFreq = interpolationForValue(coeff::hfFrequencies, parameters.hfFreqHz);
    state.hmfGain = interpolationForValue(coeff::gainValues, juce::jlimit(coeff::gainValues.front(), coeff::gainValues.back(), parameters.hmfGainDb));
    state.hmfFreq = interpolationForValue(coeff::hmfFrequencies, parameters.hmfFreqHz);
    state.hmfQ = interpolationForValue(coeff::qValues, parameters.hmfQ);
    state.lmfGain = interpolationForValue(coeff::gainValues, juce::jlimit(coeff::gainValues.front(), coeff::gainValues.back(), parameters.lmfGainDb));
    state.lmfFreq = interpolationForValue(coeff::lmfFrequencies, parameters.lmfFreqHz);
    state.lmfQ = interpolationForValue(coeff::qValues, parameters.lmfQ);
    state.lfGain = interpolationForValue(coeff::gainValues, juce::jlimit(coeff::gainValues.front(), coeff::gainValues.back(), parameters.lfGainDb));
    state.lfFreq = interpolationForValue(coeff::lfFrequencies, parameters.lfFreqHz);
    return state;
}

void Processor::updateCoefficients(PathState& pathState, const LookupState& state) noexcept
{
    if (!state.enabled)
    {
        initialiseLinearPhaseKernel(pathState);
        pathState.staticAutoGainDb = 0.0f;
        updateOutputGainTarget(pathState, false);
        return;
    }

    const auto& hf = lookupHf(state);
    const auto& hmf = lookupHmf(state);
    const auto& lmf = lookupLmf(state);
    const auto& lf = lookupLf(state);

    constexpr int positiveBinCount = kLinearPhaseDesignFftSize / 2 + 1;
    std::array<double, positiveBinCount> magnitudes {};

    for (int binIndex = 0; binIndex < positiveBinCount; ++binIndex)
    {
        const auto proportion = static_cast<double>(binIndex) / static_cast<double>(positiveBinCount - 1);
        const auto frequency = juce::jmax(1.0, proportion * currentSampleRate * 0.5);
        magnitudes[static_cast<size_t>(binIndex)] = magnitudeForBiquad(hf, frequency, currentSampleRate)
                                                  * magnitudeForBiquad(hmf, frequency, currentSampleRate)
                                                  * magnitudeForBiquad(lmf, frequency, currentSampleRate)
                                                  * magnitudeForBiquad(lf, frequency, currentSampleRate);
    }

    for (int tapIndex = 0; tapIndex < kLinearPhaseKernelSize; ++tapIndex)
    {
        const auto shiftedIndex = tapIndex - kLinearPhaseLatencySamples;
        double value = magnitudes.front();

        for (int binIndex = 1; binIndex < positiveBinCount - 1; ++binIndex)
        {
            const auto angle = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(binIndex * shiftedIndex)
                             / static_cast<double>(kLinearPhaseDesignFftSize);
            value += 2.0 * magnitudes[static_cast<size_t>(binIndex)] * std::cos(angle);
        }

        value += magnitudes.back() * std::cos(juce::MathConstants<double>::pi * static_cast<double>(shiftedIndex));
        pathState.firKernel[static_cast<size_t>(tapIndex)] = static_cast<float>((value / static_cast<double>(kLinearPhaseDesignFftSize))
                                                     * static_cast<double>(blackmanWindow(tapIndex, kLinearPhaseKernelSize)));
    }

    pathState.staticAutoGainDb = state.enabled ? estimateAutoGainDb(state, currentSampleRate) : 0.0f;
    updateOutputGainTarget(pathState, state.enabled);
}

void Processor::updateAdaptiveAutoGain(PathState& pathState, double dryBlockPower, double wetBlockPower, int numSamples, bool enabled) noexcept
{
    if (!enabled)
    {
        pathState.adaptiveAutoGainDb = 0.0f;
        updateOutputGainTarget(pathState, false);
        return;
    }

    if (dryBlockPower <= kAutoGainPowerFloor || wetBlockPower <= kAutoGainPowerFloor || currentSampleRate <= 0.0)
        return;

    const auto blockSeconds = static_cast<double>(numSamples) / currentSampleRate;
    const auto smoothing = std::exp(-blockSeconds / kAdaptiveAutoGainTimeSeconds);
    pathState.dryPowerAverage = pathState.dryPowerAverage * smoothing + dryBlockPower * (1.0 - smoothing);
    pathState.wetPowerAverage = pathState.wetPowerAverage * smoothing + wetBlockPower * (1.0 - smoothing);

    const auto correctionDb = static_cast<float>(10.0 * std::log10((pathState.dryPowerAverage + kAutoGainPowerFloor)
                                                                   / (pathState.wetPowerAverage + kAutoGainPowerFloor)));
    pathState.adaptiveAutoGainDb = juce::jlimit(-kAdaptiveAutoGainLimitDb, kAdaptiveAutoGainLimitDb, correctionDb);
    updateOutputGainTarget(pathState, true);
}

void Processor::updateOutputGainTarget(PathState& pathState, bool enabled) noexcept
{
    const auto totalGainDb = enabled ? juce::jlimit(-18.0f, 18.0f, pathState.staticAutoGainDb + pathState.adaptiveAutoGainDb) : 0.0f;
    pathState.outputGain.setTargetValue(juce::Decibels::decibelsToGain(totalGainDb));
}

void Processor::process(juce::AudioBuffer<float>& buffer, juce::AudioProcessorValueTreeState& state) noexcept
{
    const auto parameters = readParameters(state);
    const auto inputGainLinear = juce::Decibels::decibelsToGain(parameters.inputGainDb);
    const auto outputGainLinear = juce::Decibels::decibelsToGain(parameters.outputGainDb);
    std::array<LookupState, params::pathCount> lookupStates;
    std::array<float, params::pathCount> analogueDrives;

    for (int pathIndex = 0; pathIndex < params::pathCount; ++pathIndex)
    {
        const auto index = static_cast<size_t>(pathIndex);
        lookupStates[index] = makeLookupState(parameters.path[index]);
        lookupStates[index].enabled = parameters.enabled;
        analogueDrives[index] = estimateAnalogueDrive(parameters.path[index]);

        if (!pathStates[index].lookupStateValid || !isSameLookupState(pathStates[index].activeLookupState, lookupStates[index]))
        {
            updateCoefficients(pathStates[index], lookupStates[index]);
            pathStates[index].activeLookupState = lookupStates[index];
            pathStates[index].lookupStateValid = true;
        }
    }

    const auto numChannels = juce::jmin(buffer.getNumChannels(), params::pathCount);
    const auto numSamples = buffer.getNumSamples();
    std::array<double, params::pathCount> dryPowerSum {};
    std::array<double, params::pathCount> wetPowerSum {};
    std::array<int, params::pathCount> processedSamples {};

    if (numChannels <= 0)
        return;

    if (numChannels == 1)
    {
        auto* monoSamples = buffer.getWritePointer(0);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const auto drySample = monoSamples[sampleIndex] * inputGainLinear;
            auto sample = drySample;

            if (parameters.enabled)
            {
                sample = processAnalogueStageSample(sample,
                                                    analogueDrives[0] * 0.55f,
                                                    estimatePreStageAsymmetry(parameters.path[0]),
                                                    parameters.path[0].modelIndex);
                sample = processEqSample(pathStates[0], sample);
                sample = processAnalogueStageSample(sample,
                                                    analogueDrives[0],
                                                    estimatePostStageAsymmetry(parameters.path[0]),
                                                    parameters.path[0].modelIndex);
            }
            else
            {
                sample = processEqSample(pathStates[0], sample);
            }

            dryPowerSum[0] += static_cast<double>(drySample) * static_cast<double>(drySample);
            wetPowerSum[0] += static_cast<double>(sample) * static_cast<double>(sample);
            ++processedSamples[0];
            monoSamples[sampleIndex] = sample * pathStates[0].outputGain.getNextValue() * outputGainLinear;
        }
    }
    else if (!parameters.midSideMode)
    {
        auto* leftSamples = buffer.getWritePointer(0);
        auto* rightSamples = buffer.getWritePointer(1);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            float* channelSamples[2] { leftSamples, rightSamples };

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto index = static_cast<size_t>(channel);
                const auto drySample = channelSamples[channel][sampleIndex] * inputGainLinear;
                auto sample = drySample;

                if (parameters.enabled)
                {
                    sample = processAnalogueStageSample(sample,
                                                        analogueDrives[index] * 0.55f,
                                                        estimatePreStageAsymmetry(parameters.path[index]),
                                                        parameters.path[index].modelIndex);
                    sample = processEqSample(pathStates[index], sample);
                    sample = processAnalogueStageSample(sample,
                                                        analogueDrives[index],
                                                        estimatePostStageAsymmetry(parameters.path[index]),
                                                        parameters.path[index].modelIndex);
                }
                else
                {
                    sample = processEqSample(pathStates[index], sample);
                }

                dryPowerSum[index] += static_cast<double>(drySample) * static_cast<double>(drySample);
                wetPowerSum[index] += static_cast<double>(sample) * static_cast<double>(sample);
                ++processedSamples[index];
                channelSamples[channel][sampleIndex] = sample * pathStates[index].outputGain.getNextValue();
            }

            auto left = leftSamples[sampleIndex];
            auto right = rightSamples[sampleIndex];
            const auto mid = (left + right) * kMidSideNormaliser;
            const auto side = (left - right) * kMidSideNormaliser * parameters.stereoWidth;
            left = (mid + side) * kMidSideNormaliser;
            right = (mid - side) * kMidSideNormaliser;
            leftSamples[sampleIndex] = left * outputGainLinear;
            rightSamples[sampleIndex] = right * outputGainLinear;
        }
    }
    else
    {
        auto* leftSamples = buffer.getWritePointer(0);
        auto* rightSamples = buffer.getWritePointer(1);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const auto dryLeft = leftSamples[sampleIndex] * inputGainLinear;
            const auto dryRight = rightSamples[sampleIndex] * inputGainLinear;
            const auto dryMid = (dryLeft + dryRight) * kMidSideNormaliser;
            const auto drySide = (dryLeft - dryRight) * kMidSideNormaliser;

            auto wetMid = dryMid;
            auto wetSide = drySide;

            if (parameters.enabled)
            {
                wetMid = processAnalogueStageSample(wetMid,
                                                    analogueDrives[0] * 0.55f,
                                                    estimatePreStageAsymmetry(parameters.path[0]),
                                                    parameters.path[0].modelIndex);
                wetMid = processEqSample(pathStates[0], wetMid);
                wetMid = processAnalogueStageSample(wetMid,
                                                    analogueDrives[0],
                                                    estimatePostStageAsymmetry(parameters.path[0]),
                                                    parameters.path[0].modelIndex);

                wetSide = processAnalogueStageSample(wetSide,
                                                     analogueDrives[1] * 0.55f,
                                                     estimatePreStageAsymmetry(parameters.path[1]),
                                                     parameters.path[1].modelIndex);
                wetSide = processEqSample(pathStates[1], wetSide);
                wetSide = processAnalogueStageSample(wetSide,
                                                     analogueDrives[1],
                                                     estimatePostStageAsymmetry(parameters.path[1]),
                                                     parameters.path[1].modelIndex);
            }
            else
            {
                wetMid = processEqSample(pathStates[0], wetMid);
                wetSide = processEqSample(pathStates[1], wetSide);
            }

            dryPowerSum[0] += static_cast<double>(dryMid) * static_cast<double>(dryMid);
            wetPowerSum[0] += static_cast<double>(wetMid) * static_cast<double>(wetMid);
            ++processedSamples[0];

            dryPowerSum[1] += static_cast<double>(drySide) * static_cast<double>(drySide);
            wetPowerSum[1] += static_cast<double>(wetSide) * static_cast<double>(wetSide);
            ++processedSamples[1];

            wetMid *= pathStates[0].outputGain.getNextValue();
            wetSide *= pathStates[1].outputGain.getNextValue();
            wetSide *= parameters.stereoWidth;

            leftSamples[sampleIndex] = (wetMid + wetSide) * kMidSideNormaliser * outputGainLinear;
            rightSamples[sampleIndex] = (wetMid - wetSide) * kMidSideNormaliser * outputGainLinear;
        }
    }

    for (int pathIndex = 0; pathIndex < params::pathCount; ++pathIndex)
    {
        if (parameters.enabled && processedSamples[static_cast<size_t>(pathIndex)] > 0)
        {
            const auto index = static_cast<size_t>(pathIndex);
            updateAdaptiveAutoGain(pathStates[index],
                                   dryPowerSum[index] / static_cast<double>(processedSamples[index]),
                                   wetPowerSum[index] / static_cast<double>(processedSamples[index]),
                                   numSamples,
                                   true);
        }
        else
        {
            updateAdaptiveAutoGain(pathStates[static_cast<size_t>(pathIndex)], 0.0, 0.0, numSamples, false);
        }
    }
}
} // namespace NEQ