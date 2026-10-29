#include "Filter2.h"
#include "VoiceFilter.h"
#include <cmath>

namespace nsynth
{
void Filter2::Svf::set (float g, float damping) noexcept
{
    k = damping;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float Filter2::Svf::process (float x, float& band, float& high) noexcept
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    band = v1;
    high = x - k * v1 - v2;
    return v2;
}

void Filter2::setSampleRate (double sr)
{
    sampleRate = sr;
    // The comb reaches down to 20 Hz. Only grows: a voice moved between layers keeps its buffer,
    // so changing the keyboard mode on the audio thread never allocates.
    const size_t needed = (size_t) std::ceil (sr / 20.0) + 4;
    if (comb.size() < needed)
        comb.assign (needed, 0.0f);
    lastCutoff = -1.0f;
    reset();
}

void Filter2::reset() noexcept
{
    main.reset();
    second.reset();
    for (auto& f : formants)
        f.reset();
    std::fill (comb.begin(), comb.end(), 0.0f);
    combWrite = 0;
    combLowpass = 0.0f;
}

float Filter2::gainFor (float hz) const noexcept
{
    const double f = juce::jlimit (10.0, 0.45 * sampleRate, (double) hz);
    return (float) std::tan (juce::MathConstants<double>::pi * f / sampleRate);
}

std::array<float, 3> Filter2::formantsFor (float vowel) noexcept
{
    static constexpr float table[5][3] = {
        { 800.0f, 1150.0f, 2900.0f },   // A
        { 400.0f, 1700.0f, 2600.0f },   // E
        { 270.0f, 2140.0f, 2950.0f },   // I
        { 450.0f, 800.0f, 2830.0f },    // O
        { 325.0f, 700.0f, 2530.0f },    // U
    };
    vowel = juce::jlimit (0.0f, 4.0f, vowel);
    const int i = juce::jmin (3, (int) vowel);
    const float t = vowel - (float) i;
    std::array<float, 3> out {};
    for (size_t f = 0; f < 3; ++f)
        out[f] = table[i][f] * std::pow (table[i + 1][f] / table[i][f], t);  // morph in log frequency
    return out;
}

float Filter2::process (float x, Filter2Type type, float cutoffHz, float res, float drive) noexcept
{
    res = juce::jlimit (0.0f, 1.0f, res);
    if (drive > 0.0f)
    {
        const float g = 1.0f + 6.0f * drive;
        x = VoiceFilter::softClip (x * g) / std::sqrt (g);
    }

    // Coefficients follow the (often per-sample modulated) cutoff in 0.1 % steps: inaudible, and
    // it saves the tan()/pow() calls while the cutoff rests.
    const bool changed = type != lastType || ! juce::exactlyEqual (res, lastRes) || std::abs (cutoffHz - lastCutoff) > 0.001f * lastCutoff;
    if (type != lastType)
        reset();
    if (changed)
    {
        lastType = type;
        lastCutoff = cutoffHz;
        lastRes = res;
    }

    switch (type)
    {
        case Filter2Type::combPlus:
        case Filter2Type::combMinus:
        {
            if (changed)
            {
                const double hz = juce::jlimit (20.0, 0.45 * sampleRate, (double) cutoffHz);
                combDelay = (float) juce::jlimit (1.0, (double) comb.size() - 3.0, sampleRate / hz);
                combFeedback = (type == Filter2Type::combPlus ? 1.0f : -1.0f) * res * 0.97f;
                combDamp = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 9000.0 / sampleRate);
            }
            const int size = (int) comb.size();
            float readPos = (float) combWrite - combDelay;
            if (readPos < 0.0f)
                readPos += (float) size;
            const int i0 = (int) readPos;
            const int i1 = i0 + 1 < size ? i0 + 1 : 0;
            const float frac = readPos - (float) i0;
            const float delayed = comb[(size_t) i0] + (comb[(size_t) i1] - comb[(size_t) i0]) * frac;
            combLowpass += (delayed - combLowpass) * combDamp;                       // damped loop
            const float y = x + combFeedback * combLowpass;
            comb[(size_t) combWrite] = std::abs (y) < 1.0e-15f ? 0.0f : y;
            combWrite = combWrite + 1 < size ? combWrite + 1 : 0;
            return y * (1.0f - 0.5f * std::abs (combFeedback));
        }

        case Filter2Type::formant:
        {
            if (changed)
            {
                // CUTOFF 20 Hz..20 kHz (log) sweeps A -> E -> I -> O -> U; RESONANCE narrows the formants.
                const float position = std::log2 (juce::jlimit (20.0f, 20000.0f, cutoffHz) / 20.0f) / std::log2 (1000.0f);
                const auto f = formantsFor (position * 4.0f);
                const float damping = 0.5f - 0.42f * res;
                for (size_t i = 0; i < 3; ++i)
                    formants[i].set (gainFor (f[i]), damping);
            }
            float band = 0.0f, high = 0.0f, out = 0.0f;
            for (size_t i = 0; i < 3; ++i)
            {
                formants[i].process (x, band, high);
                out += band * formantGains[i];
            }
            return out * 1.4f;
        }

        case Filter2Type::lp12: case Filter2Type::lp24: case Filter2Type::hp12: case Filter2Type::hp24:
        case Filter2Type::bandpass: case Filter2Type::notch:
            break;
    }

    if (changed)
    {
        const float g = gainFor (cutoffHz);
        main.set (g, 2.0f - 1.94f * res);                    // Q 0.5 .. ~16
        second.set (g, juce::MathConstants<float>::sqrt2);   // the extra 12 dB without a second peak
    }
    float band = 0.0f, high = 0.0f;
    const float low = main.process (x, band, high);
    switch (type)
    {
        case Filter2Type::lp12:     return low;
        case Filter2Type::hp12:     return high;
        case Filter2Type::bandpass: return band * main.k;    // unity gain at the centre
        case Filter2Type::notch:    return low + high;
        case Filter2Type::lp24:     { float b2, h2; return second.process (low, b2, h2); }
        case Filter2Type::hp24:     { float b2, h2; second.process (high, b2, h2); return h2; }
        case Filter2Type::combPlus: case Filter2Type::combMinus: case Filter2Type::formant: break;
    }
    return low;
}
} // namespace nsynth
