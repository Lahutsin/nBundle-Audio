#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace NComp::params
{
static constexpr auto input1 = "input1";
static constexpr auto input2 = "input2";
static constexpr auto output1 = "output1";
static constexpr auto output2 = "output2";
static constexpr auto threshold1 = "threshold1";
static constexpr auto threshold2 = "threshold2";
static constexpr auto ratio1 = "ratio1";
static constexpr auto ratio2 = "ratio2";
static constexpr auto attack1 = "attack1";
static constexpr auto attack2 = "attack2";
static constexpr auto release1 = "release1";
static constexpr auto release2 = "release2";
static constexpr auto mix = "mix";
static constexpr auto character = "character";
static constexpr auto link = "link";
static constexpr auto linkAmount = "linkAmount";
static constexpr auto mslr = "mslr";
static constexpr auto power = "power";
static constexpr auto autoGain = "autoGain";
static constexpr auto ab = "ab";
static constexpr auto sidechain = "sidechain";
static constexpr auto sidechainListen = "sidechainListen";
static constexpr auto sidechainHpf = "sidechainHpf";
static constexpr auto sidechainTilt = "sidechainTilt";
static constexpr auto meterL = "meterL";
static constexpr auto meterR = "meterR";

inline constexpr std::array<float, 9> kAttackLegendValues { 1.0f, 5.0f, 10.0f, 20.0f, 40.0f, 80.0f, 160.0f, 320.0f, 640.0f };
inline constexpr std::array<float, 9> kReleaseLegendValues { 5.0f, 50.0f, 100.0f, 200.0f, 1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f };
inline constexpr std::array<const char*, 6> kCharacterNames { "Standard", "VCA Glue", "FET Punch", "Opto Smooth", "Vari-Mu", "Tube" };
inline constexpr std::array<const char*, 6> kCharacterLegendLabels { "STD", "VCA", "FET", "OPT", "MU", "TUB" };
inline constexpr std::array<float, 6> kCharacterLegendProportions { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };

inline int characterIndexFromValue(float value)
{
    return juce::jlimit(0, static_cast<int>(kCharacterNames.size()) - 1, juce::roundToInt(value));
}

inline juce::String characterNameFromValue(float value)
{
    return kCharacterNames[static_cast<size_t>(characterIndexFromValue(value))];
}

inline float characterValueFromText(const juce::String& text, float fallbackValue)
{
    const auto token = text.trim().toUpperCase().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

    for (size_t index = 0; index < kCharacterNames.size(); ++index)
    {
        const auto label = juce::String(kCharacterNames[index]).toUpperCase().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        const auto legend = juce::String(kCharacterLegendLabels[index]).toUpperCase().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

        if (token == label || token == legend)
            return static_cast<float>(index);
    }

    if (token == "VARIMU")
        return 4.0f;

    if (token == "TUBE" || token == "TUB")
        return 5.0f;

    if (token == "OPTO")
        return 3.0f;

    if (token == "FET")
        return 2.0f;

    if (token == "VCA")
        return 1.0f;

    if (token == "STANDARD" || token == "STD" || token == "CLEAN")
        return 0.0f;

    const auto numeric = text.getFloatValue();
    if (numeric != 0.0f || text.containsAnyOf("0123456789"))
        return static_cast<float>(juce::jlimit(0, static_cast<int>(kCharacterNames.size()) - 1, juce::roundToInt(numeric)));

    return fallbackValue;
}

template <size_t N>
inline float piecewiseValueFromNormalised(const std::array<float, N>& anchors, float proportion)
{
    proportion = juce::jlimit(0.0f, 1.0f, proportion);

    if (proportion <= 0.0f)
        return anchors.front();

    if (proportion >= 1.0f)
        return anchors.back();

    const float scaled = proportion * static_cast<float>(N - 1);
    const auto index = static_cast<size_t>(juce::jlimit(0, static_cast<int>(N) - 2, static_cast<int>(scaled)));
    const float segmentProportion = scaled - static_cast<float>(index);
    return juce::jmap(segmentProportion, anchors[index], anchors[index + 1]);
}

template <size_t N>
inline float piecewiseNormalisedFromValue(const std::array<float, N>& anchors, float value)
{
    value = juce::jlimit(anchors.front(), anchors.back(), value);

    for (size_t i = 1; i < N; ++i)
    {
        if (value <= anchors[i])
        {
            const float start = anchors[i - 1];
            const float end = anchors[i];
            const float span = end - start;
            const float segmentProportion = span > 0.0f ? (value - start) / span : 0.0f;
            return (static_cast<float>(i - 1) + segmentProportion) / static_cast<float>(N - 1);
        }
    }

    return 1.0f;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto dBRange = NormalisableRange<float>{ -24.0f, 24.0f, 0.001f };
    auto attackRange = NormalisableRange<float>{
        kAttackLegendValues.front(),
        kAttackLegendValues.back(),
        [] (float, float, float proportion)
        {
            return piecewiseValueFromNormalised(kAttackLegendValues, proportion);
        },
        [] (float, float, float value)
        {
            return piecewiseNormalisedFromValue(kAttackLegendValues, value);
        }
    };
    auto releaseRange = NormalisableRange<float>{
        kReleaseLegendValues.front(),
        kReleaseLegendValues.back(),
        [] (float, float, float proportion)
        {
            return piecewiseValueFromNormalised(kReleaseLegendValues, proportion);
        },
        [] (float, float, float value)
        {
            return piecewiseNormalisedFromValue(kReleaseLegendValues, value);
        }
    };

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ input1, 1 }, "Input L", dBRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ input2, 1 }, "Input R", dBRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ output1, 1 }, "Output L", dBRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ output2, 1 }, "Output R", dBRange, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ threshold1, 1 }, "Threshold L", NormalisableRange<float>{ -32.0f, 0.0f, 0.001f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ threshold2, 1 }, "Threshold R", NormalisableRange<float>{ -32.0f, 0.0f, 0.001f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ratio1, 1 }, "Ratio L", NormalisableRange<float>{ 2.0f, 9.0f, 0.1f }, 2.0f, AudioParameterFloatAttributes{}.withLabel("X")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ ratio2, 1 }, "Ratio R", NormalisableRange<float>{ 2.0f, 9.0f, 0.1f }, 2.0f, AudioParameterFloatAttributes{}.withLabel("X")));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ attack1, 1 }, "Attack L", attackRange, 80.0f, AudioParameterFloatAttributes{}.withLabel("ms")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ attack2, 1 }, "Attack R", attackRange, 80.0f, AudioParameterFloatAttributes{}.withLabel("ms")));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ release1, 1 }, "Release L", releaseRange, 200.0f, AudioParameterFloatAttributes{}.withLabel("ms")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ release2, 1 }, "Release R", releaseRange, 200.0f, AudioParameterFloatAttributes{}.withLabel("ms")));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ mix, 1 }, "Mix", NormalisableRange<float>{ 0.0f, 100.0f, 1.0f }, 100.0f, AudioParameterFloatAttributes{}.withLabel("%")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ character, 1 }, "Character", NormalisableRange<float>{ 0.0f, 5.0f, 1.0f }, 0.0f));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ linkAmount, 1 }, "Link Amount", NormalisableRange<float>{ 0.0f, 100.0f, 0.1f }, 100.0f, AudioParameterFloatAttributes{}.withLabel("%")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ sidechainHpf, 1 }, "SC HPF", NormalisableRange<float>{ 30.0f, 220.0f, 0.1f }, 90.0f, AudioParameterFloatAttributes{}.withLabel("Hz")));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ sidechainTilt, 1 }, "SC Tilt", NormalisableRange<float>{ -6.0f, 6.0f, 0.01f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("dB")));

    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ link, 1 }, "Link", StringArray{ "On", "Off" }, 0));
    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ mslr, 1 }, "L/R | M/S", StringArray{ "L/R", "M/S" }, 0));
    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ power, 1 }, "Power", StringArray{ "On", "Off" }, 0));
    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ autoGain, 1 }, "Auto Gain", StringArray{ "Off", "On" }, 0));

    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ ab, 1 }, "A/B", StringArray{ "A", "B" }, 0));
    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ sidechain, 1 }, "Sidechain", StringArray{ "Off", "On" }, 0));
    params.emplace_back(std::make_unique<AudioParameterChoice>(ParameterID{ sidechainListen, 1 }, "SC Listen", StringArray{ "Off", "On" }, 0));

    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ meterL, 1 }, "Meter L", NormalisableRange<float>{ 0.0f, 1.0f, 0.0001f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("peak").withAutomatable(false)));
    params.emplace_back(std::make_unique<AudioParameterFloat>(ParameterID{ meterR, 1 }, "Meter R", NormalisableRange<float>{ 0.0f, 1.0f, 0.0001f }, 0.0f, AudioParameterFloatAttributes{}.withLabel("peak").withAutomatable(false)));

    return { params.begin(), params.end() };
}
} // namespace NComp::params
