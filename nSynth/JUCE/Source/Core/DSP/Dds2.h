#pragma once

#include <juce_core/juce_core.h>
#include "Dds1.h"
#include "../Params/Enums.h"

namespace nsynth
{
// Two-point polyBLEP step corrector with a one-sample output delay, so that
// a discontinuity can correct both the sample before and the sample after it.
class BlepDelayLine
{
public:
    void reset (float value = 0.0f) noexcept { previous = value; current = 0.0f; }
    void begin (float naive) noexcept { current = naive; }

    // `jump` = value after - value before; `samplesSince` = time since the step (0..1).
    void addStep (float jump, float samplesSince) noexcept
    {
        const float d = juce::jlimit (0.0f, 1.0f, samplesSince);
        previous += jump * 0.5f * d * d;
        current -= jump * 0.5f * (1.0f - d) * (1.0f - d);
    }

    float finish() noexcept
    {
        const float out = previous;
        previous = current;
        return out;
    }

private:
    float previous { 0.0f }, current { 0.0f };
};

// DDS 2: algorithmic oscillator core, phase reset on every note.
class Dds2
{
public:
    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void noteOn() noexcept { phase = 0.0; }
    void reset() noexcept { phase = 0.0; blep.reset(); subBlep.reset(); subFlip = false; lastOutput = 0.0f; }

    // Output is delayed by one sample (BLEP look-ahead). Pass the DDS1 centroid
    // wrap of the same sample when SYNC is active, otherwise nullptr.
    float process (double frequencyHz, Dds2Wave wave, float pulseWidth, const WrapEvent* syncMaster, juce::Random& noise) noexcept;

    // Sub oscillator: flip-flop divider of the DDS1 centroid (one octave below DDS1).
    // Also delayed by one sample to stay aligned with process().
    float processSub (bool sine, double dds1CentroidPhase, const WrapEvent& dds1Wrap) noexcept;

    double getPhase() const noexcept { return phase; }
    float getLastOutput() const noexcept { return lastOutput; }

    static float naive (Dds2Wave wave, double phase, float pulseWidth) noexcept;

private:
    double sampleRate { 44100.0 };
    double phase { 0.0 };
    BlepDelayLine blep, subBlep;
    bool subFlip { false };
    float lastOutput { 0.0f };
};
} // namespace nsynth
