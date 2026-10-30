#include "Dds1.h"

namespace nsynth
{
float Dds1::coreOffset (int core) noexcept
{
    // Relative frequency offsets of a 7-voice unison oscillator stack at full detune.
    static constexpr float offsets[kNumCores] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f,
                                                  0.01991221f, 0.06216538f, 0.10745242f };
    return offsets[core];
}

float Dds1::superDetuneCurve (float knob) noexcept
{
    static constexpr double c[] = { 10028.7312891634, -50818.8652045924, 111363.4808729368, -138150.6761080548,
                                    106649.6679158292, -53046.9642751875, 17019.9518580080, -3425.0836591318,
                                    404.2703938388, -24.1878824391, 0.6717417634, 0.0030115596 };
    const double x = juce::jlimit (0.0, 1.0, (double) knob);
    double y = 0.0;
    for (double coefficient : c)
        y = y * x + coefficient;
    return knob <= 0.0f ? 0.0f : (float) juce::jlimit (0.0, 1.0, y);
}

void Dds1::coreGains (SuperMode mode, float& centroidGain, float& sisterGain) noexcept
{
    if (mode == SuperMode::off)
    {
        centroidGain = 1.0f;
        sisterGain = 0.0f;
        return;
    }

    // Super saw mix curves (centre and side levels) at mix 0.5 ("1/2") and 1.0 ("On"),
    // then RMS-normalised so the level into the filter stays comparable.
    const float x = mode == SuperMode::half ? 0.5f : 1.0f;
    const float c = -0.55366f * x + 0.99785f;
    const float s = -0.73764f * x * x + 1.2841f * x + 0.044372f;
    const float norm = 1.0f / std::sqrt (c * c + 6.0f * s * s);
    centroidGain = c * norm;
    sisterGain = s * norm;
}

void Dds1::setSampleRate (double sr) noexcept
{
    sampleRate = sr;
    harmonicLimit = MipWavetable::maxHarmonicFraction (sr);
    blendIncrement.fill (-1.0);
}

const MipWavetable::Blend& Dds1::blendFor (int core, double increment) noexcept
{
    // Pitch modulation moves the increment every sample; a relative change below 1e-4 moves a fade
    // weight by less than 6e-4, so the cached blend is kept until the increment really moves.
    auto& cached = blendIncrement[(size_t) core];
    if (std::abs (increment - cached) > 1.0e-4 * std::abs (cached))
    {
        cached = increment;
        blends[(size_t) core] = MipWavetable::blendForIncrement (increment, harmonicLimit);
    }
    return blends[(size_t) core];
}

void Dds1::randomisePhases (juce::Random& rng) noexcept
{
    for (auto& p : phases)
        p = rng.nextDouble();
}

void Dds1::resetPhases() noexcept
{
    phases.fill (0.0);
}

void Dds1::noteOn (SuperMode mode) noexcept
{
    if (mode == SuperMode::half)
        resetPhases();
}

float Dds1::readWave (Dds1Wave wave, const MipWavetable::Blend& blend, double phase, const Settings& s, float noiseSample) const noexcept
{
    auto classic = [&] (int index) { return waveBank->classic (index).read (blend, phase); };

    float a = 0.0f, b = 0.0f;
    switch (wave)
    {
        case Dds1Wave::sine:     a = classic (0); if (s.morph > 0.0f) b = classic (1); break;
        case Dds1Wave::saw:      a = classic (1); if (s.morph > 0.0f) b = classic (2); break;
        case Dds1Wave::square:   a = classic (2); if (s.morph > 0.0f) b = classic (3); break;
        case Dds1Wave::triangle: a = classic (3); b = noiseSample; break;
        case Dds1Wave::noise:    a = noiseSample; if (s.morph > 0.0f) b = waveBank->alt (s.altA).read (blend, phase); break;
        case Dds1Wave::alt:
            a = waveBank->alt (s.altA).read (blend, phase);
            if (s.morph > 0.0f) b = waveBank->alt (s.altB).read (blend, phase);
            break;
    }
    return a + (b - a) * s.morph;
}

void Dds1::advance (double& phase, double inc, WrapEvent* wrap) noexcept
{
    // Advance first, then read: the returned sample belongs to the end of this sample period, so
    // a wrap reported here happened `fractionAfter` samples ago. A negative increment (through-zero
    // linear FM) runs the phase backwards and wraps at 0 instead of 1.
    phase += inc;
    if (phase >= 1.0 || phase < 0.0)
    {
        const bool forward = phase >= 1.0;
        phase -= std::floor (phase);
        if (wrap != nullptr)
        {
            wrap->wrapped = true;
            const double since = forward ? phase : 1.0 - phase;
            wrap->fractionAfter = std::abs (inc) > 0.0 ? (float) juce::jlimit (0.0, 1.0, since / std::abs (inc)) : 0.0f;
        }
    }
}

float Dds1::process (double frequencyHz, const Settings& s, juce::Random& noise, WrapEvent& centroidWrap) noexcept
{
    jassert (waveBank != nullptr);
    centroidWrap = {};

    const double centreInc = frequencyHz / sampleRate;
    const float spread = superDetuneCurve (s.detune);
    float centroidGain = 1.0f, sisterGain = 0.0f;
    coreGains (s.superMode, centroidGain, sisterGain);

    const bool needsNoise = s.wave == Dds1Wave::noise || (s.wave == Dds1Wave::triangle && s.morph > 0.0f);
    const float noiseSample = needsNoise ? noise.nextFloat() * 2.0f - 1.0f : 0.0f;

    if (s.superMode == SuperMode::off)
    {
        auto& phase = phases[(size_t) kCentroid];
        advance (phase, centreInc, &centroidWrap);

        if (s.wave == Dds1Wave::noise && s.morph <= 0.0f)
            return noiseSample;

        return readWave (s.wave, blendFor (kCentroid, centreInc), phase, s, noiseSample);
    }

    float out = 0.0f;
    for (int core = 0; core < kNumCores; ++core)
    {
        const bool isCentroid = core == kCentroid;
        float offset = coreOffset (core) * spread;
        if (s.mirrorDetune)
            offset = -offset;

        const double inc = centreInc * (1.0 + (double) offset);
        auto& phase = phases[(size_t) core];

        advance (phase, inc, isCentroid ? &centroidWrap : nullptr);

        const float gain = isCentroid ? centroidGain : sisterGain;
        if (gain > 0.0f)
        {
            out += gain * readWave (s.wave, blendFor (core, inc), phase, s, noiseSample);
        }
    }

    if (s.wave == Dds1Wave::noise && s.morph <= 0.0f)
        return noiseSample; // seven noise cores are still white noise

    return out;
}
} // namespace nsynth
