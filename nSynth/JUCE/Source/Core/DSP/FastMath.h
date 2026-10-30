#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace nsynth::fastmath
{
// Per-sample replacements for libm calls in the voice path. Each is far more accurate than
// audible (errors below 1e-7 relative, i.e. < 0.001 cent for pitch) and several times cheaper.

// Round to nearest without a libm call (no SSE4.1 round instruction is assumed). |x| < 2^62.
inline double roundNearest (double x) noexcept
{
    return (double) (int64_t) (x >= 0.0 ? x + 0.5 : x - 0.5);
}

// 2^x for |x| < 126. Range reduction to [-0.5, 0.5] and a degree-7 Taylor polynomial.
inline double exp2 (double x) noexcept
{
    const double rounded = roundNearest (x);
    const double f = (x - rounded) * 0.6931471805599453;   // ln 2
    const double poly = 1.0 + f * (1.0 + f * (0.5 + f * (1.0 / 6.0 + f * (1.0 / 24.0 + f * (1.0 / 120.0
                       + f * (1.0 / 720.0 + f * (1.0 / 5040.0)))))));
    const auto exponent = (int64_t) rounded + 1023;
    uint64_t bits = (uint64_t) exponent << 52;
    double scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return poly * scale;
}

inline float exp2 (float x) noexcept
{
    return (float) exp2 ((double) (x < -126.0f ? -126.0f : x > 126.0f ? 126.0f : x));
}

// log2(x) for finite x > 0: exponent bits plus an atanh series for the mantissa.
inline double log2 (double x) noexcept
{
    uint64_t bits;
    std::memcpy (&bits, &x, sizeof (bits));
    const int exponent = (int) ((bits >> 52) & 0x7ff) - 1023;
    bits = (bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
    double m;
    std::memcpy (&m, &bits, sizeof (m));             // 1 <= m < 2
    double e = (double) exponent;
    if (m > 1.4142135623730951)
    {
        m *= 0.5;
        e += 1.0;
    }
    const double t = (m - 1.0) / (m + 1.0);          // |t| <= 0.1716
    const double t2 = t * t;
    const double lnM = 2.0 * t * (1.0 + t2 * (1.0 / 3.0 + t2 * (1.0 / 5.0 + t2 * (1.0 / 7.0 + t2 * (1.0 / 9.0 + t2 / 11.0)))));
    return e + lnM * 1.4426950408889634;             // 1 / ln 2
}

// sin(2 pi phase) for any finite phase (in cycles).
inline double sin2pi (double phase) noexcept
{
    double r = phase - roundNearest (phase);        // [-0.5, 0.5]
    if (r > 0.25)       r = 0.5 - r;
    else if (r < -0.25) r = -0.5 - r;                 // [-0.25, 0.25]
    const double x = r * 6.283185307179586;
    const double x2 = x * x;
    return x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0
              + x2 * (-1.0 / 39916800.0 + x2 * (1.0 / 6227020800.0)))))));
}

// tan(x) for 0 <= x < pi/2 (bilinear-transform prewarping). Continued-fraction approximation
// below pi/4, reflected above it.
inline double tanPrewarp (double x) noexcept
{
    constexpr double quarterPi = 0.7853981633974483, halfPi = 1.5707963267948966;
    const bool reflect = x > quarterPi;
    const double y = reflect ? halfPi - x : x;
    const double y2 = y * y;
    const double t = y * (135135.0 + y2 * (-17325.0 + y2 * (378.0 - y2)))
                   / (135135.0 + y2 * (-62370.0 + y2 * (3150.0 - 28.0 * y2)));
    return reflect ? 1.0 / t : t;
}
} // namespace nsynth::fastmath
