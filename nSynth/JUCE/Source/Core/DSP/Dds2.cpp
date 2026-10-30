#include "Dds2.h"
#include "FastMath.h"

namespace nsynth
{
namespace
{
constexpr int kLatency = BlepDelayLine::kLatency;
constexpr int kResolution = 256;         // table points per sample, linearly interpolated
constexpr double kCutoff = 0.47;         // sinc cutoff, fraction of the sample rate
constexpr double kKaiserBeta = 7.0;
constexpr int kTableLength = 2 * kLatency * kResolution + 2; // + 1 so samplesSince = 1 can interpolate

// step: the band-limited step h, 0 -> 1 over the kernel; ramp: the band-limited ramp minus the
// naive one (the BLAMP residual). Both indexed by (time from the edge + kLatency) x kResolution.
struct BlepTables
{
    std::array<float, kTableLength> step {}, ramp {};
};

double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50 && term > 1.0e-12 * sum; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

BlepTables makeBlepTables()
{
    // Integrate the windowed sinc (the impulse) and then the step on a 16x finer grid.
    constexpr int fine = 16, points = 2 * kLatency * kResolution * fine;
    constexpr double dt = 1.0 / (kResolution * fine);
    auto impulse = [] (double t)
    {
        const double x = t / kLatency;
        if (std::abs (x) >= 1.0)
            return 0.0;
        const double a = juce::MathConstants<double>::twoPi * kCutoff * t;
        const double sinc = std::abs (a) < 1.0e-9 ? 1.0 : std::sin (a) / a;
        return 2.0 * kCutoff * sinc * besselI0 (kKaiserBeta * std::sqrt (1.0 - x * x)) / besselI0 (kKaiserBeta);
    };

    std::vector<double> step ((size_t) points + 1), ramp ((size_t) points + 1);
    for (int i = 1; i <= points; ++i)
    {
        const double t = -kLatency + i * dt;
        step[(size_t) i] = step[(size_t) i - 1] + 0.5 * dt * (impulse (t - dt) + impulse (t));
    }
    const double total = step.back();
    for (auto& v : step)
        v /= total;
    double integral = 0.0;
    for (int i = 1; i <= points; ++i)
    {
        integral += 0.5 * dt * (step[(size_t) i - 1] + step[(size_t) i]);
        ramp[(size_t) i] = integral - juce::jmax (0.0, -kLatency + i * dt);
    }
    // The residual must end at exactly 0, or every edge would leave a tiny offset behind.
    const double drift = ramp.back();
    for (int i = 0; i <= points; ++i)
        ramp[(size_t) i] -= drift * i / points;

    BlepTables tables;
    for (int i = 0; i < kTableLength - 1; ++i)
    {
        tables.step[(size_t) i] = (float) step[(size_t) (i * fine)];
        tables.ramp[(size_t) i] = (float) ramp[(size_t) (i * fine)];
    }
    tables.step.back() = 1.0f;
    return tables;
}

// Built at load time, so the audio thread never computes or allocates it.
const BlepTables blepTables = makeBlepTables();

void addNaturalEdges (BlepDelayLine& blep, Dds2Wave wave, double start, double length, double inc, float extra, float pw) noexcept
{
    const double end = start + length;
    auto since = [&] (double edge) { return (float) ((end - edge) / inc) + extra; };
    auto slopeJump = [&] (double phase) { return (Dds2::naiveSlope (wave, phase, pw) - Dds2::naiveSlope (wave, phase - 1.0e-9, pw)) * (float) inc; };

    switch (wave)
    {
        case Dds2Wave::saw:
            if (end >= 1.0)
            {
                blep.addStep (-2.0f, since (1.0));
                // The convex ramp is steeper at its start than at its end.
                blep.addRamp ((Dds2::naiveSlope (wave, 0.0, pw) - Dds2::naiveSlope (wave, 1.0 - 1.0e-9, pw)) * (float) inc, since (1.0));
            }
            break;

        case Dds2Wave::square:
        case Dds2Wave::pulse:
        {
            // The plateaus droop, so each edge is a little smaller than 2 and also bends the slope.
            const float edgeJump = 2.0f - (float) WaveBank::kSquareDroop;
            if (end >= 1.0)
            {
                blep.addStep (edgeJump, since (1.0));
                blep.addRamp ((Dds2::naiveSlope (wave, 0.0, pw) - Dds2::naiveSlope (wave, 1.0 - 1.0e-9, pw)) * (float) inc, since (1.0));
            }
            for (double edge : { (double) pw, 1.0 + (double) pw })
                if (start < edge && end >= edge)
                {
                    blep.addStep (-edgeJump, since (edge));
                    blep.addRamp (slopeJump ((double) pw), since (edge));
                }
            break;
        }

        case Dds2Wave::sine:
        case Dds2Wave::triangle: // rounded tips: the slope is continuous
        case Dds2Wave::noise:
            break;
    }
}
} // namespace

void BlepDelayLine::reset() noexcept
{
    buffer.fill (0.0f);
    position = 0;
}

void BlepDelayLine::addStep (float jump, float samplesSince) noexcept
{
    const float x = juce::jlimit (0.0f, 1.0f, samplesSince) * (float) kResolution;
    const int offset = (int) x;
    const float frac = x - (float) offset;
    const float* h = blepTables.step.data() + offset;
    for (int tap = 0; tap < 2 * kLatency; ++tap, h += kResolution)
    {
        // The naive step is already in the samples from the edge on (tap >= kLatency).
        const float residual = h[0] + (h[1] - h[0]) * frac - (tap >= kLatency ? 1.0f : 0.0f);
        buffer[(size_t) ((position + tap - kLatency) & (kSize - 1))] += jump * residual;
    }
}

void BlepDelayLine::addRamp (float slopeJump, float samplesSince) noexcept
{
    const float x = juce::jlimit (0.0f, 1.0f, samplesSince) * (float) kResolution;
    const int offset = (int) x;
    const float frac = x - (float) offset;
    const float* q = blepTables.ramp.data() + offset;
    for (int tap = 0; tap < 2 * kLatency; ++tap, q += kResolution)
        buffer[(size_t) ((position + tap - kLatency) & (kSize - 1))] += slopeJump * (q[0] + (q[1] - q[0]) * frac);
}

float BlepDelayLine::finish() noexcept
{
    auto& oldest = buffer[(size_t) ((position - kLatency) & (kSize - 1))];
    const float out = oldest;
    oldest = 0.0f;
    position = (position + 1) & (kSize - 1);
    return out;
}

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

float Dds2::naiveMean (Dds2Wave wave, float pulseWidth) noexcept
{
    const double k = WaveBank::kRampCurve, droop = WaveBank::kSquareDroop;
    switch (wave)
    {
        // 2 x mean of (1 - e^-kp) / (1 - e^-k) - 1: the convex ramp sits a little above zero.
        case Dds2Wave::saw:   return (float) (2.0 * (1.0 - (1.0 - std::exp (-k)) / k) / (1.0 - std::exp (-k)) - 1.0);
        // Each plateau averages to its level minus half the droop.
        case Dds2Wave::pulse: return (float) ((2.0 * juce::jlimit (0.02, 0.98, (double) pulseWidth) - 1.0) * (1.0 - 0.5 * droop));
        case Dds2Wave::sine:
        case Dds2Wave::square:
        case Dds2Wave::triangle:
        case Dds2Wave::noise: break;
    }
    return 0.0f;
}

float Dds2::naiveSlope (Dds2Wave wave, double p, float pulseWidth) noexcept
{
    const double droop = WaveBank::kSquareDroop;
    p -= std::floor (p);
    switch (wave)
    {
        case Dds2Wave::sine:     return (float) (juce::MathConstants<double>::twoPi * std::cos (juce::MathConstants<double>::twoPi * p));
        case Dds2Wave::saw:      return (float) (2.0 * WaveBank::kRampCurve * std::exp (-WaveBank::kRampCurve * p) / (1.0 - std::exp (-WaveBank::kRampCurve)));
        case Dds2Wave::square:   return (float) (p < 0.5 ? -droop / 0.5 : droop / 0.5);
        case Dds2Wave::pulse:    return (float) (p < (double) pulseWidth ? -droop / pulseWidth : droop / (1.0 - pulseWidth));
        case Dds2Wave::triangle:
        {
            const double t = p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
            return (float) ((1.5 - 1.5 * t * t) * (p < 0.25 || p >= 0.75 ? 4.0 : -4.0));
        }
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
        blep.addRamp ((naiveSlope (wave, 0.0, pw) - naiveSlope (wave, resetPhase, pw)) * (float) inc, fa);
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
