#pragma once

#include <juce_core/juce_core.h>
#include "../Params/Enums.h"

namespace nsynth
{
// 1-pole HPF (6 dB/oct) followed by one of four analog-modelled low-pass filters:
//   Moog 24    transistor ladder, tanh saturation in every stage and at the input (Huovilainen
//              model); 24 dB/oct, the classic fat, slightly bass-shy resonance.
//   Moog 12    the same ladder tapped after the second stage: 12 dB/oct with a ladder resonance.
//   Jupiter 24 OTA cascade (IR3109 style): each stage saturates the difference of its input and
//              output, smoother and brighter at high resonance.
//   SEM 12     state-variable filter (Oberheim SEM style) with a saturating band-pass integrator.
// All are zero-delay-feedback (TPT) structures. The non-linear terms are linearised around the
// previous estimate (tanh(x) ~ x * tanh(x0)/x0) and the resulting linear system is solved exactly;
// two passes per sample keep the cutoff and resonance accurate at high drive. See docs/03-DSP-SPEC.md §4.
class VoiceFilter
{
public:
    // Ladder feedback at RES = 1: above the linear self-oscillation threshold of 4, so the
    // resonance sings strongly and is limited by the stage saturation.
    static constexpr float kMaxFeedback = 4.5f;

    void setSampleRate (double sr) noexcept;
    void reset() noexcept;

    // cutoffs in Hz, res 0..1
    void setParameters (float hpfHz, float lpfHz, float res, Drive drive, FilterType type = FilterType::moog24) noexcept;

    float process (float input) noexcept;

    float getFeedbackGain() const noexcept { return k; }

    // tanh approximation (exact to ~1e-3, clamps beyond +/-3) and tanh(x)/x.
    static float softClip (float x) noexcept;
    static float softClipRatio (float x) noexcept;

private:
    static float prewarp (float hz, double sr) noexcept;
    float processLadder (float u0) noexcept;
    float processSvf (float x) noexcept;

    double sampleRate { 44100.0 };
    Drive drive { Drive::off };
    FilterType type { FilterType::moog24 };
    float lastHpfHz { -1.0f }, lastLpfHz { -1.0f }, lastRes { -1.0f };
    float hpG { 0.0f }, hpState { 0.0f };
    float g { 0.0f }, k { 0.0f }, svfDamping { 1.0f };
    float s[4] {}, y[4] {};
    float lastInput { 0.0f };
    float inputGain { 1.0f }, outputGain { 1.0f }, bassCompensation { 0.0f };
    uint32_t noiseSeed { 0x1234567u };
};
} // namespace nsynth
