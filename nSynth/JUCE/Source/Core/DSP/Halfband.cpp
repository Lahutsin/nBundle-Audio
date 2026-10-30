#include "Halfband.h"
#include <juce_core/juce_core.h>
#include <complex>

namespace nsynth
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;

double integerPower (double x, int n) noexcept
{
    double result = 1.0;
    while (n-- > 0)
        result *= x;
    return result;
}

// Theta-function series of the elliptic design.
double numeratorSeries (double q, int order, int c) noexcept
{
    double sum = 0.0, term = 0.0;
    int sign = 1;
    for (int i = 0; i == 0 || std::abs (term) > 1.0e-100; ++i, sign = -sign)
    {
        term = integerPower (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * pi / order) * sign;
        sum += term;
    }
    return sum;
}

double denominatorSeries (double q, int order, int c) noexcept
{
    double sum = 0.0, term = 0.0;
    int sign = -1;
    for (int i = 1; i == 1 || std::abs (term) > 1.0e-100; ++i, sign = -sign)
    {
        term = integerPower (q, i * i) * std::cos (i * 2 * c * pi / order) * sign;
        sum += term;
    }
    return sum;
}
} // namespace

Halfband::Design Halfband::designFor (double baseSampleRate)
{
    // Transition band around the oversampled quarter rate (= the base Nyquist): from 20 kHz to
    // fs - 20 kHz, as a fraction of the oversampled rate.
    const double transition = juce::jlimit (0.01, 0.1, (0.5 * baseSampleRate - 20000.0) / (2.0 * baseSampleRate));

    double k = std::tan ((1.0 - transition * 2.0) * pi / 4.0);
    k *= k;
    const double kk = std::pow (1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk), e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

    const double stopPower = std::pow (10.0, -kAttenuationDb / 10.0), a = stopPower / (1.0 - stopPower);
    int order = (int) std::ceil (std::log (a * a / 16.0) / std::log (q));
    order |= 1;
    order = juce::jlimit (3, 2 * kMaxCoefficients + 1, order);

    Design design;
    design.count = (order - 1) / 2;
    for (int i = 0; i < design.count; ++i)
    {
        const double w = numeratorSeries (q, order, i + 1) * std::pow (q, 0.25) / (denominatorSeries (q, order, i + 1) + 0.5);
        const double w2 = w * w;
        const double x = std::sqrt ((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
        design.coefficients[(size_t) i] = (float) ((1.0 - x) / (1.0 + x));
    }
    return design;
}

double Halfband::responseDb (const Design& design, double frequencyHz, double baseSampleRate)
{
    const auto z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * frequencyHz / (2.0 * baseSampleRate));
    const auto z2 = z1 * z1;
    std::complex<double> a0 (1.0), a1 (1.0);
    for (int i = 0; i < design.count; ++i)
    {
        const double c = design.coefficients[(size_t) i];
        (i % 2 == 0 ? a0 : a1) *= (c + z2) / (1.0 + c * z2);
    }
    return 20.0 * std::log10 (std::abs (0.5 * (a0 + z1 * a1)));
}
} // namespace nsynth
