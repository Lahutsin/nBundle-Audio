#pragma once

#include <juce_core/juce_core.h>

namespace nsynth
{
// Analog-style envelope: Attack Hold -> Attack -> Decay Hold -> Decay -> Sustain -> Release.
// Segments are RC-like exponentials. Times are "full-scale" times:
// attack 0 -> 1, decay 1 -> 0 (towards sustain it is correspondingly shorter), release 1 -> 0.
class Envelope
{
public:
    enum class Stage { idle, attackHold, attack, decayHold, decay, sustain, release };

    struct Settings
    {
        float attackHold { 0.0f }; // s, 0 = stage skipped
        float attack { 0.001f };   // s
        float decayHold { 0.0f };  // s
        float decay { 0.5f };      // s
        float sustain { 1.0f };    // 0..1
        float release { 0.1f };    // s
        bool loop { false };       // ENV1 loop mode: attack/decay-hold/decay repeat while gate is on
    };

    // Overshoot ratios of the RC model: attack charges towards 1 + kAttackRatio,
    // decay/release discharge towards target - kDecayRatio (=> ~ -60 dB end point).
    static constexpr float kAttackRatio = 0.3f;
    static constexpr float kDecayRatio = 0.001f;
    static constexpr float kIdleThreshold = 1.0e-4f;

    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void setSettings (const Settings& s) noexcept;

    // Retrigger starts the attack from the current level (no click) [analog behaviour].
    void noteOn() noexcept;
    void noteOff() noexcept;
    void reset() noexcept { stage = Stage::idle; value = 0.0f; holdCounter = 0; }

    float process() noexcept;

    float getValue() const noexcept { return value; }
    Stage getStage() const noexcept { return stage; }
    bool isActive() const noexcept { return stage != Stage::idle; }
    bool isReleasing() const noexcept { return stage == Stage::release; }

    // Per-sample coefficient for a full-scale segment of `seconds` with overshoot `ratio`.
    static float coefficientFor (float seconds, float ratio, double sampleRate) noexcept;

private:
    void enterAttack() noexcept;
    void enterDecayHold() noexcept;
    void enterDecay() noexcept;

    double sampleRate { 44100.0 };
    Settings settings;
    Stage stage { Stage::idle };
    float value { 0.0f };
    int64_t holdCounter { 0 };
    float attackCoef { 1.0f }, decayCoef { 1.0f }, releaseCoef { 1.0f };
};
} // namespace nsynth
