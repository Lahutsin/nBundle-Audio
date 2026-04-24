#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace NEQ::params
{
static constexpr auto eqIn = "eqIn";
static constexpr auto msMode = "msMode";
static constexpr auto linkChannels = "linkChannels";
static constexpr auto eqModel = "eqModel";
static constexpr auto stereoWidth = "stereoWidth";
static constexpr auto inputGain = "inputGain";
static constexpr auto outputGain = "outputGain";
static constexpr auto blackModeBase = "blackMode";
static constexpr auto hfGainBase = "hfGain";
static constexpr auto hfFreqBase = "hfFreq";
static constexpr auto hfBellBase = "hfBell";
static constexpr auto hfEnableBase = "hfEnable";
static constexpr auto hmfGainBase = "hmfGain";
static constexpr auto hmfFreqBase = "hmfFreq";
static constexpr auto hmfQBase = "hmfQ";
static constexpr auto hmfEnableBase = "hmfEnable";
static constexpr auto lmfGainBase = "lmfGain";
static constexpr auto lmfFreqBase = "lmfFreq";
static constexpr auto lmfQBase = "lmfQ";
static constexpr auto lmfEnableBase = "lmfEnable";
static constexpr auto lfGainBase = "lfGain";
static constexpr auto lfFreqBase = "lfFreq";
static constexpr auto lfBellBase = "lfBell";
static constexpr auto lfEnableBase = "lfEnable";

static constexpr int pathCount = 2;
static constexpr int kEqModelCount = 5;
static constexpr int kEqModelDefault = 0;

enum EqModel
{
    modelA = 0,
    modelB,
    modelC,
    modelD,
    modelF
};

inline constexpr std::array<const char*, kEqModelCount> kEqModelNames { "Mode A", "Mode B", "Mode C", "Mode D", "Mode F" };

struct PathParameterIds
{
    juce::String blackMode;
    juce::String hfGain;
    juce::String hfFreq;
    juce::String hfBell;
    juce::String hfEnable;
    juce::String hmfGain;
    juce::String hmfFreq;
    juce::String hmfQ;
    juce::String hmfEnable;
    juce::String lmfGain;
    juce::String lmfFreq;
    juce::String lmfQ;
    juce::String lmfEnable;
    juce::String lfGain;
    juce::String lfFreq;
    juce::String lfBell;
    juce::String lfEnable;
};

inline juce::String parameterIdForPath(int pathIndex, const char* baseId)
{
    return pathIndex == 0 ? juce::String(baseId) : juce::String(baseId) + "B";
}

inline PathParameterIds idsForPath(int pathIndex)
{
    return {
        parameterIdForPath(pathIndex, blackModeBase),
        parameterIdForPath(pathIndex, hfGainBase),
        parameterIdForPath(pathIndex, hfFreqBase),
        parameterIdForPath(pathIndex, hfBellBase),
        parameterIdForPath(pathIndex, hfEnableBase),
        parameterIdForPath(pathIndex, hmfGainBase),
        parameterIdForPath(pathIndex, hmfFreqBase),
        parameterIdForPath(pathIndex, hmfQBase),
        parameterIdForPath(pathIndex, hmfEnableBase),
        parameterIdForPath(pathIndex, lmfGainBase),
        parameterIdForPath(pathIndex, lmfFreqBase),
        parameterIdForPath(pathIndex, lmfQBase),
        parameterIdForPath(pathIndex, lmfEnableBase),
        parameterIdForPath(pathIndex, lfGainBase),
        parameterIdForPath(pathIndex, lfFreqBase),
        parameterIdForPath(pathIndex, lfBellBase),
        parameterIdForPath(pathIndex, lfEnableBase)
    };
}

inline constexpr std::array<float, 8> kHfFrequencies { 1500.0f, 2000.0f, 3000.0f, 5000.0f, 8000.0f, 12000.0f, 16000.0f, 18000.0f };
inline constexpr std::array<float, 9> kHmfFrequencies { 600.0f, 800.0f, 1000.0f, 1500.0f, 2000.0f, 3000.0f, 4500.0f, 6000.0f, 7000.0f };
inline constexpr std::array<float, 9> kLmfFrequencies { 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f, 1500.0f, 2000.0f, 2500.0f };
inline constexpr std::array<float, 9> kLfFrequencies { 30.0f, 40.0f, 60.0f, 100.0f, 150.0f, 220.0f, 300.0f, 400.0f, 450.0f };
inline constexpr std::array<float, 10> kQValues { 0.2f, 0.35f, 0.5f, 0.8f, 1.2f, 1.8f, 2.8f, 4.0f, 5.5f, 7.0f };

template <size_t N>
inline float piecewiseValueFromNormalised(const std::array<float, N>& anchors, float proportion)
{
    proportion = juce::jlimit(0.0f, 1.0f, proportion);

    if (proportion <= 0.0f)
        return anchors.front();

    if (proportion >= 1.0f)
        return anchors.back();

    const auto scaled = proportion * static_cast<float>(N - 1);
    const auto index = static_cast<size_t>(juce::jlimit(0, static_cast<int>(N) - 2, static_cast<int>(scaled)));
    const auto segment = scaled - static_cast<float>(index);
    return juce::jmap(segment, anchors[index], anchors[index + 1]);
}

template <size_t N>
inline float piecewiseNormalisedFromValue(const std::array<float, N>& anchors, float value)
{
    value = juce::jlimit(anchors.front(), anchors.back(), value);

    for (size_t index = 1; index < N; ++index)
    {
        if (value <= anchors[index])
        {
            const auto start = anchors[index - 1];
            const auto end = anchors[index];
            const auto span = end - start;
            const auto segment = span > 0.0f ? (value - start) / span : 0.0f;
            return (static_cast<float>(index - 1) + segment) / static_cast<float>(N - 1);
        }
    }

    return 1.0f;
}

template <size_t N>
inline juce::NormalisableRange<float> makeAnchoredRange(const std::array<float, N>& anchors)
{
    return {
        anchors.front(),
        anchors.back(),
        [&anchors] (float, float, float proportion)
        {
            return piecewiseValueFromNormalised(anchors, proportion);
        },
        [&anchors] (float, float, float value)
        {
            return piecewiseNormalisedFromValue(anchors, value);
        }
    };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

    const auto gainRange = NormalisableRange<float>{ -15.0f, 15.0f, 0.1f };
    const auto hmfFreqRange = makeAnchoredRange(kHmfFrequencies);
    const auto lmfFreqRange = makeAnchoredRange(kLmfFrequencies);
    const auto hfFreqRange = makeAnchoredRange(kHfFrequencies);
    const auto lfFreqRange = makeAnchoredRange(kLfFrequencies);
    const auto qRange = makeAnchoredRange(kQValues);

    parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ eqIn, 1 }, "IN", true));
    parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ msMode, 1 }, "M/S", false));
    parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ linkChannels, 1 }, "LINK", true));
    parameters.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ eqModel, 1 }, "EQ Model", StringArray{ "Mode A", "Mode B", "Mode C", "Mode D", "Mode F" }, kEqModelDefault));
    parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ stereoWidth, 1 }, "Stereo Width", NormalisableRange<float>{ 0.0f, 2.0f, 0.01f }, 1.0f, AudioParameterFloatAttributes{}.withLabel("V")));
    parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ inputGain, 1 }, "Input Gain", NormalisableRange<float>{ -12.0f, 12.0f, 0.01f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
    parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ outputGain, 1 }, "Output Gain", NormalisableRange<float>{ -12.0f, 12.0f, 0.01f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));

    for (int pathIndex = 0; pathIndex < pathCount; ++pathIndex)
    {
        const auto ids = idsForPath(pathIndex);
        const auto pathName = pathIndex == 0 ? juce::String("Path A") : juce::String("Path B");

        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.blackMode, 1 }, pathName + " BLK", false));

        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.hfGain, 1 }, pathName + " HF Gain", gainRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.hfFreq, 1 }, pathName + " HF Frequency", hfFreqRange, 6500.0f, AudioParameterFloatAttributes{}.withLabel("Hz")));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.hfBell, 1 }, pathName + " HF Bell", false));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.hfEnable, 1 }, pathName + " HF Enable", true));

        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.hmfGain, 1 }, pathName + " HMF Gain", gainRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.hmfFreq, 1 }, pathName + " HMF Frequency", hmfFreqRange, 2000.0f, AudioParameterFloatAttributes{}.withLabel("Hz")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.hmfQ, 1 }, pathName + " HMF Q", qRange, 1.5f));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.hmfEnable, 1 }, pathName + " HMF Enable", true));

        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.lmfGain, 1 }, pathName + " LMF Gain", gainRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.lmfFreq, 1 }, pathName + " LMF Frequency", lmfFreqRange, 800.0f, AudioParameterFloatAttributes{}.withLabel("Hz")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.lmfQ, 1 }, pathName + " LMF Q", qRange, 1.5f));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.lmfEnable, 1 }, pathName + " LMF Enable", true));

        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.lfGain, 1 }, pathName + " LF Gain", gainRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
        parameters.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ids.lfFreq, 1 }, pathName + " LF Frequency", lfFreqRange, 150.0f, AudioParameterFloatAttributes{}.withLabel("Hz")));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.lfBell, 1 }, pathName + " LF Bell", false));
        parameters.emplace_back(std::make_unique<AudioParameterBool>(ParameterID{ ids.lfEnable, 1 }, pathName + " LF Enable", true));
    }

    return { parameters.begin(), parameters.end() };
}
} // namespace NEQ::params