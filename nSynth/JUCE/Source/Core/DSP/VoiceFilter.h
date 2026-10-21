#pragma once

#include <juce_core/juce_core.h>
#include "../Params/Enums.h"

namespace nsynth
{
// 1-pole HPF (6 dB/oct) followed by a 4-pole resonant ladder LPF (24 dB/oct).
// The ladder is a topology-preserving (zero-delay-feedback) model with a tanh
// saturator in the resonance feedback path; the implicit feedback equation is
// solved per sample with Newton-Raphson. See docs/03-DSP-SPEC.md §4.
class VoiceFilter
{
public:
    // Feedback gain at RES = 1. A linear ladder self-oscillates at k = 4; the tanh
    // in the loop needs a little more to sustain a stable oscillation.
    static constexpr float kMaxFeedback = 4.4f;

    void setSampleRate (double sr) noexcept;
    void reset() noexcept;

    // cutoffs in Hz, res 0..1
    void setParameters (float hpfHz, float lpfHz, float res, Drive drive) noexcept;

    float process (float input) noexcept;

    float getFeedbackGain() const noexcept { return k; }

private:
    static float prewarp (float hz, double sr) noexcept;
    static float fastTanh (float x) noexcept;
    static float stageSaturate (float x, Drive drive) noexcept;

    double sampleRate { 44100.0 };
    double invSampleRate { 1.0 / 44100.0 };
    Drive drive { Drive::off };
    float lastHpfHz { -1.0f }, lastLpfHz { -1.0f }, lastRes { -1.0f };
    float hpG { 0.0f }, hpState { 0.0f };
    float G { 0.0f }, k { 0.0f };
    float s[4] {};
    float lastY { 0.0f };
    float inputGain { 1.0f }, invDriveTanh { 1.0f }, compensation { 1.0f };
    uint32_t noiseSeed { 0x1234567u };
};
} // namespace nsynth
