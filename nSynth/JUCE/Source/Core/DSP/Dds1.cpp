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

void Dds1::coreGains (SuperMode mode, float& centroidGain, float& sisterGain) noexcept
{
    if (mode == SuperMode::off)
    {
        centroidGain = 1.0f;
        sisterGain = 0.0f;
        return;
    }

    // [ASSUMPTION] "half depth" = sister mix 0.5, "on" = 1.0 using the supersaw mix curves,
    // then RMS-normalised so the level into the filter stays comparable.
    const float x = mode == SuperMode::half ? 0.5f : 1.0f;
    const float c = -0.55366f * x + 0.99785f;
    const float s = -0.73764f * x * x + 1.2841f * x + 0.044372f;
    const float norm = 1.0f / std::sqrt (c * c + 6.0f * s * s);
    centroidGain = c * norm;
    sisterGain = s * norm;
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

float Dds1::readWave (Dds1Wave wave, int level, double phase, const Settings& s, float noiseSample) const noexcept
{
    auto classic = [&] (int index) { return waveBank->classic (index).read (level, phase); };

    float a = 0.0f, b = 0.0f;
    switch (wave)
    {
        case Dds1Wave::sine:     a = classic (0); if (s.morph > 0.0f) b = classic (1); break;
        case Dds1Wave::saw:      a = classic (1); if (s.morph > 0.0f) b = classic (2); break;
        case Dds1Wave::square:   a = classic (2); if (s.morph > 0.0f) b = classic (3); break;
        case Dds1Wave::triangle: a = classic (3); b = noiseSample; break;
        case Dds1Wave::noise:    a = noiseSample; if (s.morph > 0.0f) b = waveBank->alt (s.altA).read (level, phase); break;
        case Dds1Wave::alt:
            a = waveBank->alt (s.altA).read (level, phase);
            if (s.morph > 0.0f) b = waveBank->alt (s.altB).read (level, phase);
            break;
    }
    return a + (b - a) * s.morph;
}

float Dds1::process (double frequencyHz, const Settings& s, juce::Random& noise, WrapEvent& centroidWrap) noexcept
{
    jassert (waveBank != nullptr);
    centroidWrap = {};

    const double centreInc = frequencyHz / sampleRate;
    const float spread = s.detune * s.detune;
    float centroidGain = 1.0f, sisterGain = 0.0f;
    coreGains (s.superMode, centroidGain, sisterGain);

    const bool needsNoise = s.wave == Dds1Wave::noise || (s.wave == Dds1Wave::triangle && s.morph > 0.0f);
    const float noiseSample = needsNoise ? noise.nextFloat() * 2.0f - 1.0f : 0.0f;

    if (s.superMode == SuperMode::off)
    {
        auto& phase = phases[(size_t) kCentroid];
        phase += centreInc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            centroidWrap.wrapped = true;
            centroidWrap.fractionAfter = centreInc > 0.0 ? (float) juce::jlimit (0.0, 1.0, phase / centreInc) : 0.0f;
        }

        if (s.wave == Dds1Wave::noise && s.morph <= 0.0f)
            return noiseSample;

        const int level = MipWavetable::levelForIncrement (centreInc);
        return readWave (s.wave, level, phase, s, noiseSample);
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

        // Advance first, then read: the returned sample belongs to the end of this
        // sample period, so a wrap reported here happened `fractionAfter` samples ago.
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            if (isCentroid)
            {
                centroidWrap.wrapped = true;
                centroidWrap.fractionAfter = inc > 0.0 ? (float) juce::jlimit (0.0, 1.0, phase / inc) : 0.0f;
            }
        }

        const float gain = isCentroid ? centroidGain : sisterGain;
        if (gain > 0.0f)
        {
            const int level = MipWavetable::levelForIncrement (inc);
            out += gain * readWave (s.wave, level, phase, s, noiseSample);
        }
    }

    if (s.wave == Dds1Wave::noise && s.morph <= 0.0f)
        return noiseSample; // seven noise cores are still white noise

    return out;
}
} // namespace nsynth
