#include "Lfo.h"

namespace nsynth
{
float Lfo::shapeValue (LfoShape s, double p, float heldValue) noexcept
{
    switch (s)
    {
        case LfoShape::triangle:   return (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case LfoShape::revSaw:     return (float) (1.0 - p);
        case LfoShape::sampleHold: return heldValue;
        case LfoShape::square:     return p < 0.5 ? 1.0f : 0.0f;
        case LfoShape::sine:       return (float) std::sin (juce::MathConstants<double>::twoPi * p);
        case LfoShape::saw:        return (float) p;
        case LfoShape::noise:      return heldValue;
    }
    return 0.0f;
}

void Lfo::noteOn (bool resetPhase, bool once, double phaseOffset, juce::Random& rng) noexcept
{
    oneShot = once;
    finished = false;
    if (resetPhase || once)
    {
        phase = phaseOffset - std::floor (phaseOffset);
        held = rng.nextFloat() * 2.0f - 1.0f;
    }
}

float Lfo::process (double rateHz, LfoShape shape, juce::Random& rng) noexcept
{
    if (shape == LfoShape::noise)
    {
        held = rng.nextFloat() * 2.0f - 1.0f;
        return held;
    }

    if (finished)
        return shapeValue (shape, 1.0, held);

    const float out = shapeValue (shape, phase, held);

    phase += juce::jmax (0.0, rateHz) / sampleRate;
    if (phase >= 1.0)
    {
        if (oneShot)
        {
            phase = 1.0;
            finished = true;
        }
        else
        {
            phase -= std::floor (phase);
            held = rng.nextFloat() * 2.0f - 1.0f;
        }
    }
    return out;
}

namespace rates
{
double lowFrequencyHz (float fader) noexcept { return 0.05 * std::pow (600.0, (double) juce::jlimit (0.0f, 1.0f, fader)); }
double highFrequencyHz (float fader) noexcept { return 20.0 * std::pow (1000.0, (double) juce::jlimit (0.0f, 1.0f, fader)); }
double hfTrackRatio (float fader) noexcept { return std::pow (2.0, ((double) juce::jlimit (0.0f, 1.0f, fader) - 0.5) * 8.0); }
double dds2LfoHz (float tune) noexcept { return 0.1 * std::pow (1000.0, ((double) juce::jlimit (-7.0f, 7.0f, tune) + 7.0) / 14.0); }

const std::array<double, 16>& lfo1SyncBeats() noexcept
{
    static const std::array<double, 16> beats { 32.0, 16.0, 8.0, 4.0, 2.0, 1.5, 4.0 / 3.0, 1.0,
                                                0.75, 2.0 / 3.0, 0.5, 0.375, 1.0 / 3.0, 0.25, 0.1875, 1.0 / 6.0 };
    return beats;
}

const std::array<double, 16>& delaySyncBeats() noexcept
{
    static const std::array<double, 16> beats { 1.0 / 12.0, 3.0 / 32.0, 1.0 / 8.0, 1.0 / 6.0, 3.0 / 16.0, 0.25, 1.0 / 3.0, 0.375,
                                                0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 8.0 / 3.0 };
    return beats;
}

double clockDivBeats (int index) noexcept
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0 };
    return beats[juce::jlimit (0, 7, index)];
}

int divisionIndex (float fader, int count) noexcept
{
    return juce::jlimit (0, count - 1, (int) (juce::jlimit (0.0f, 1.0f, fader) * (float) count));
}
} // namespace rates
} // namespace nsynth
