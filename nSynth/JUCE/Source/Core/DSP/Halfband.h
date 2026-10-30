#pragma once

#include <array>
#include <cstddef>

namespace nsynth
{
// 2x up- and down-sampling around the voice filters, so their saturation runs at twice the rate.
// A polyphase IIR half-band low-pass: H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2, with A0 and A1 chains
// of first-order all-pass sections (elliptic design after Valenzuela & Constantinides). The pass
// band reaches 20 kHz; from fs - 20 kHz up everything is kAttenuationDb down, so whatever folds
// back across the base rate's Nyquist lands above 20 kHz.
class Halfband
{
public:
    static constexpr int kMaxCoefficients = 12;
    static constexpr double kAttenuationDb = 90.0;

    struct Design
    {
        std::array<float, kMaxCoefficients> coefficients {}; // even indices: A0, odd: A1
        int count { 0 };
    };

    // For a base (not oversampled) sample rate.
    static Design designFor (double baseSampleRate);
    // |H| in dB at a frequency of the oversampled signal (for tests and analysis).
    static double responseDb (const Design& design, double frequencyHz, double baseSampleRate);
};

class HalfbandResampler
{
public:
    void setDesign (const Halfband::Design& newDesign) noexcept { design = newDesign; reset(); }
    void reset() noexcept
    {
        xState.fill (0.0f);
        yState.fill (0.0f);
        previousOdd = 0.0f;
    }

    // One base-rate sample in, the two oversampled ones out (unity gain).
    void up (float in, float& first, float& second) noexcept
    {
        first = branch (0, in);
        second = branch (1, in);
    }

    // Two oversampled samples in, one base-rate sample out.
    float down (float first, float second) noexcept
    {
        const float out = 0.5f * (branch (0, first) + branch (1, previousOdd));
        previousOdd = second;
        return out;
    }

private:
    // The all-pass chain of one branch; each coefficient belongs to exactly one branch, so the
    // states can share one array.
    float branch (int index, float v) noexcept
    {
        for (int i = index; i < design.count; i += 2)
        {
            const float out = design.coefficients[(std::size_t) i] * (v - yState[(std::size_t) i]) + xState[(std::size_t) i];
            xState[(std::size_t) i] = v;
            yState[(std::size_t) i] = out;
            v = out;
        }
        return v;
    }

    Halfband::Design design;
    std::array<float, Halfband::kMaxCoefficients> xState {}, yState {};
    float previousOdd { 0.0f };
};
} // namespace nsynth
