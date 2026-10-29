#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "../Params/Enums.h"

namespace nsynth
{
// FILTER 2: a multimode per-voice filter after (serial) or beside (parallel) the analog VCF.
//   LP/HP 12, Band Pass, Notch  zero-delay-feedback state-variable filter (Cytomic/TPT SVF)
//   LP/HP 24                    the SVF followed by a non-resonant SVF stage
//   Comb +/-                    feedback comb tuned to CUTOFF, RESONANCE = feedback, damped loop
//   Formant                     three band-pass formants; CUTOFF morphs the vowel A-E-I-O-U
// DRIVE saturates the input (tanh), RESONANCE 1 stays below self-oscillation.
class Filter2
{
public:
    void setSampleRate (double sr);
    void reset() noexcept;
    float process (float input, Filter2Type type, float cutoffHz, float res, float drive) noexcept;

    // Vowel formants (F1-F3 in Hz) for a morph position 0..4 (A, E, I, O, U).
    static std::array<float, 3> formantsFor (float vowel) noexcept;

private:
    struct Svf
    {
        float a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f }, k { 2.0f };
        float ic1 { 0.0f }, ic2 { 0.0f };
        void set (float g, float damping) noexcept;
        // Returns low-pass; band-pass and high-pass through the references.
        float process (float x, float& band, float& high) noexcept;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };

    float gainFor (float hz) const noexcept;

    double sampleRate { 96000.0 };
    Filter2Type lastType { Filter2Type::lp12 };
    float lastCutoff { -1.0f }, lastRes { -1.0f };
    Svf main, second;
    std::array<Svf, 3> formants;
    std::array<float, 3> formantGains { 1.0f, 0.5f, 0.25f };
    std::vector<float> comb;
    int combWrite { 0 };
    float combDelay { 1.0f }, combFeedback { 0.0f }, combDamp { 0.0f }, combLowpass { 0.0f };
};
} // namespace nsynth
