#include "Dds2.h"
#include "FastMath.h"

namespace nsynth
{
namespace
{
void addNaturalEdges (BlepDelayLine& blep, Dds2Wave wave, double start, double length, double inc, float extra, float pw) noexcept
{
    const double end = start + length;
    auto since = [&] (double edge) { return (float) ((end - edge) / inc) + extra; };

    switch (wave)
    {
        case Dds2Wave::saw:
            if (end >= 1.0)
                blep.addStep (-2.0f, since (1.0));
            break;

        case Dds2Wave::square:
        case Dds2Wave::pulse:
        {
            // The plateaus droop, so each edge is a little smaller than 2.
            const float edgeJump = 2.0f - (float) WaveBank::kSquareDroop;
            if (end >= 1.0)
                blep.addStep (edgeJump, since (1.0));
            for (double edge : { (double) pw, 1.0 + (double) pw })
                if (start < edge && end >= edge)
                    blep.addStep (-edgeJump, since (edge));
            break;
        }

        case Dds2Wave::sine:
        case Dds2Wave::triangle:
        case Dds2Wave::noise:
            break;
    }
}
} // namespace

float Dds2::naive (Dds2Wave wave, double p, float pulseWidth) noexcept
{
    // Same analog-style shapes as the DDS1 tables: convex saw ramp, drooping plateaus, rounded triangle.
    auto drooping = [] (double phase, double width)
    {
        const double droop = WaveBank::kSquareDroop;
        return (float) (phase < width ? 1.0 - droop * phase / width : -1.0 + droop * (phase - width) / (1.0 - width));
    };
    switch (wave)
    {
        case Dds2Wave::sine:     return (float) fastmath::sin2pi (p);
        case Dds2Wave::saw:      return (float) (2.0 * WaveBank::analogRamp (p) - 1.0);
        case Dds2Wave::square:   return drooping (p, 0.5);
        case Dds2Wave::pulse:    return drooping (p, (double) pulseWidth);
        case Dds2Wave::triangle:
            return (float) WaveBank::roundTriangle (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case Dds2Wave::noise:    return 0.0f;
    }
    return 0.0f;
}

float Dds2::process (double frequencyHz, Dds2Wave wave, float pulseWidth, const WrapEvent* syncMaster, juce::Random& noise) noexcept
{
    const double inc = juce::jlimit (0.0, 0.5, frequencyHz / sampleRate);
    const float pw = wave == Dds2Wave::square ? 0.5f : juce::jlimit (0.02f, 0.98f, pulseWidth);
    const double start = phase;

    if (wave == Dds2Wave::noise)
    {
        phase = std::fmod (start + inc, 1.0);
        blep.begin (noise.nextFloat() * 2.0f - 1.0f);
        lastOutput = blep.finish();
        return lastOutput;
    }

    if (syncMaster != nullptr && syncMaster->wrapped && inc > 0.0)
    {
        const float fa = syncMaster->fractionAfter;
        const double beforeReset = inc * (1.0 - (double) fa);
        double resetPhase = start + beforeReset;
        resetPhase -= std::floor (resetPhase);

        phase = std::fmod ((double) fa * inc, 1.0);
        blep.begin (naive (wave, phase, pw));
        addNaturalEdges (blep, wave, start, beforeReset, inc, fa, pw);
        blep.addStep (naive (wave, 0.0, pw) - naive (wave, resetPhase, pw), fa);
    }
    else
    {
        const double end = start + inc;
        phase = end - std::floor (end);
        blep.begin (naive (wave, phase, pw));
        if (inc > 0.0)
            addNaturalEdges (blep, wave, start, inc, inc, 0.0f, pw);
    }

    lastOutput = blep.finish();
    return lastOutput;
}

float Dds2::processSub (bool sine, double dds1CentroidPhase, const WrapEvent& dds1Wrap) noexcept
{
    if (dds1Wrap.wrapped)
        subFlip = ! subFlip;

    const double subPhase = (subFlip ? 0.5 : 0.0) + 0.5 * dds1CentroidPhase;

    if (sine)
    {
        subBlep.begin ((float) std::sin (juce::MathConstants<double>::twoPi * subPhase));
    }
    else
    {
        subBlep.begin (subPhase < 0.5 ? 1.0f : -1.0f);
        if (dds1Wrap.wrapped)
            subBlep.addStep (subFlip ? -2.0f : 2.0f, dds1Wrap.fractionAfter);
    }
    return subBlep.finish();
}
} // namespace nsynth
