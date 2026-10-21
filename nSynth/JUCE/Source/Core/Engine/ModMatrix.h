#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <memory>

namespace nsynth
{
enum class ModSource : int
{
    dds2, lfo2, env1, velocity, polyAftertouch, expression, ribbon, noteNumber, perNotePitch, timbre, count
};

enum class ModTarget : int
{
    lfo1Rate, crossMod, waveMod, oscillatorMix, hpfCutoff, lpfResonance, env1Decay, delayTime, count
};

enum class DirectModTarget : int
{
    dds2Tune, lpfCutoff, vcfEnvAmount, vcfLfo1Amount, vcfDds2Amount,
    vcaEnvLevel, vcaLfo1Amount, vcaDds2Amount, env1Attack, env1Sustain,
    env1Release, env2Attack, env2Decay, env2Sustain, env2Release,
    lfo1Delay, lfo1LrPhase, lfo2Rate, lfo2Delay, ddsModLfo1Amount,
    pwDetune, portamento, delaySend, delayFeedback, count
};

struct ModMatrixData
{
    static constexpr int sourceCount = (int) ModSource::count;
    static constexpr int targetCount = (int) ModTarget::count;
    static constexpr int directTargetCount = (int) DirectModTarget::count;

    std::array<std::array<float, targetCount>, sourceCount> fixed {};
    std::array<std::array<float, directTargetCount>, sourceCount> direct {};
};

class ModMatrix
{
public:
    ModMatrix();

    std::shared_ptr<const ModMatrixData> snapshot() const noexcept;
    float getFixedAmount (ModSource source, ModTarget target) const noexcept;
    float getDirectAmount (ModSource source, DirectModTarget target) const noexcept;
    bool hasSource (ModSource source) const noexcept;
    void restoreSnapshot (const ModMatrixData& snapshot);

    void setFixedAmount (ModSource source, ModTarget target, float amount);
    bool setDirectAmount (ModSource source, DirectModTarget target, float amount);
    void clearAll();
    void clearSource (ModSource source);
    void clearTarget (ModTarget target);
    void clearDirectTarget (DirectModTarget target);

    juce::ValueTree toValueTree (int layerIndex) const;
    bool restoreFromValueTree (const juce::ValueTree& tree, juce::String* error = nullptr);

    static constexpr int schemaVersion = 2;
    static bool isPanelDuplicate (ModSource source, DirectModTarget target) noexcept;
    static const char* getSourceName (ModSource source) noexcept;
    static const char* getTargetName (ModTarget target) noexcept;
    static const char* getDirectTargetName (DirectModTarget target) noexcept;

private:
    void publish();
    ModMatrixData data;
    std::shared_ptr<const ModMatrixData> published;
};
} // namespace nsynth
