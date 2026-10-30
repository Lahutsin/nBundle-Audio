#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include "Dds1.h"
#include "../Params/Enums.h"

namespace nsynth
{
// Band-limited step (BLEP) and ramp (BLAMP) corrector. The kernel is a Kaiser-windowed sinc
// (linear phase, 2 x kLatency taps): a step or a slope change inside the current sample adds
// the difference between the band-limited and the naive edge to the kLatency samples on either
// side of it, so the output is kLatency samples late.
class BlepDelayLine
{
public:
    static constexpr int kLatency = 12;

    void reset() noexcept;
    void begin (float naive) noexcept { buffer[(size_t) position] += naive; }

    // `jump` = value after - value before; `samplesSince` = time since the edge (0..1).
    void addStep (float jump, float samplesSince) noexcept;
    // `slopeJump` = slope after - slope before, per sample.
    void addRamp (float slopeJump, float samplesSince) noexcept;

    float finish() noexcept;

private:
    static constexpr int kSize = 32; // ring buffer, a power of two above 2 x kLatency
    static_assert (kSize > 2 * kLatency && (kSize & (kSize - 1)) == 0);

    std::array<float, kSize> buffer {};
    int position { 0 };
};

// DDS 2: algorithmic oscillator core, phase reset on every note.
class Dds2
{
public:
    static constexpr int kLatency = BlepDelayLine::kLatency;

    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void noteOn() noexcept { phase = 0.0; }
    void reset() noexcept { phase = 0.0; blep.reset(); subBlep.reset(); subFlip = false; lastOutput = 0.0f; }

    // Output is delayed by kLatency samples (BLEP look-ahead). Pass the DDS1 centroid
    // wrap of the same sample when SYNC is active, otherwise nullptr.
    float process (double frequencyHz, Dds2Wave wave, float pulseWidth, const WrapEvent* syncMaster, juce::Random& noise) noexcept;

    // Sub oscillator: flip-flop divider of the DDS1 centroid (one octave below DDS1).
    // Also delayed by kLatency samples to stay aligned with process().
    float processSub (bool sine, double dds1CentroidPhase, const WrapEvent& dds1Wrap) noexcept;

    double getPhase() const noexcept { return phase; }
    float getLastOutput() const noexcept { return lastOutput; }

    static float naive (Dds2Wave wave, double phase, float pulseWidth) noexcept;
    // Average of the naive waveform over one cycle (its DC offset).
    static float naiveMean (Dds2Wave wave, float pulseWidth) noexcept;
    // d naive / d phase (per cycle).
    static float naiveSlope (Dds2Wave wave, double phase, float pulseWidth) noexcept;

private:
    double sampleRate { 44100.0 };
    double phase { 0.0 };
    BlepDelayLine blep, subBlep;
    bool subFlip { false };
    float lastOutput { 0.0f };
};
} // namespace nsynth
