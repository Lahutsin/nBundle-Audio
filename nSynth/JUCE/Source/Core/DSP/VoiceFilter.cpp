#include "VoiceFilter.h"

namespace nsynth
{
void VoiceFilter::setSampleRate (double sr) noexcept
{
    sampleRate = sr;
    invSampleRate = 1.0 / juce::jmax (1.0, sr);
    lastHpfHz = lastLpfHz = lastRes = -1.0f;
}

float VoiceFilter::fastTanh (float x) noexcept
{
    if (x >= 4.97f) return 1.0f;
    if (x <= -4.97f) return -1.0f;
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}

float VoiceFilter::stageSaturate (float x, Drive d) noexcept
{
    if (d == Drive::off)
        return x;
    const float a = d == Drive::one ? 0.015f : 0.045f;
    const float ax2 = a * x * x;
    return x * (1.0f - ax2 / (3.0f + ax2)) + (d == Drive::two ? 0.008f * x * x / (1.0f + x * x) : 0.0f);
}

float VoiceFilter::prewarp (float hz, double sr) noexcept
{
    const double f = juce::jlimit (1.0, 0.49 * sr, (double) hz);
    return (float) std::tan (juce::MathConstants<double>::pi * f / sr);
}

void VoiceFilter::reset() noexcept
{
    hpState = 0.0f;
    for (auto& v : s)
        v = 0.0f;
    lastY = 0.0f;
}

void VoiceFilter::setParameters (float hpfHz, float lpfHz, float res, Drive newDrive) noexcept
{
    if (std::abs (hpfHz - lastHpfHz) > 1.0e-4f * lastHpfHz + 1.0e-5f)
    {
        lastHpfHz = hpfHz;
        const float gh = prewarp (hpfHz, sampleRate);
        hpG = gh / (1.0f + gh);
    }

    if (std::abs (lpfHz - lastLpfHz) > 1.0e-4f * lastLpfHz + 1.0e-5f)
    {
        lastLpfHz = lpfHz;
        const float g = prewarp (lpfHz, sampleRate);
        G = g / (1.0f + g);
    }

    if (std::abs (res - lastRes) > 1.0e-6f || drive != newDrive)
    {
        lastRes = res;
        k = juce::jlimit (0.0f, 1.0f, res) * kMaxFeedback;
        drive = newDrive;
        switch (drive)
        {
            // OFF: clean; the SSI-style ladder loses some passband level with resonance.
            case Drive::off:
                inputGain = 1.0f;
                invDriveTanh = 1.0f;
                compensation = 1.0f;
                break;
            // 1: subtle saturation with resonance compensation.
            case Drive::one:
                inputGain = 1.6f;
                invDriveTanh = 1.0f / std::tanh (1.6f);
                compensation = 1.0f + 0.85f * k;
                break;
            // 2: overdrive.
            case Drive::two:
                inputGain = 4.0f;
                invDriveTanh = 1.0f;
                compensation = 1.0f + 0.35f * k;
                break;
        }
    }
}

float VoiceFilter::process (float x) noexcept
{
    // Analog noise floor (~ -140 dBFS) lets the self-oscillating filter start on its own.
    noiseSeed ^= noiseSeed << 13; noiseSeed ^= noiseSeed >> 17; noiseSeed ^= noiseSeed << 5;
    x += ((float) (noiseSeed & 0xffff) / 65535.0f - 0.5f) * 2.0e-7f;

    // 1-pole TPT high-pass
    const float v = (x - hpState) * hpG;
    const float lp = v + hpState;
    hpState = lp + v;
    float u = x - lp;

    if (drive == Drive::one)
        u = fastTanh (inputGain * u) * invDriveTanh;
    else if (drive == Drive::two)
        u = fastTanh (inputGain * u + 0.04f * u * u);

    // 4-pole ZDF ladder: y4 = G^4 (u - k tanh(y4)) + S
    const float beta = 1.0f - G;
    const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
    const float S = G3 * beta * s[0] + G2 * beta * s[1] + G * beta * s[2] + beta * s[3];

    float y = lastY;
    for (int iter = 0; iter < 5; ++iter)
    {
        const float t = fastTanh (y);
        const float f = y - G4 * (u - k * t) - S;
        if (std::abs (f) < 1.0e-7f)
            break;
        const float fp = 1.0f + G4 * k * (1.0f - t * t);
        y -= f / fp;
    }

    const float u1 = u - k * fastTanh (y);
    const float y1 = stageSaturate (G * u1 + beta * s[0], drive);
    const float y2 = stageSaturate (G * y1 + beta * s[1], drive);
    const float y3 = stageSaturate (G * y2 + beta * s[2], drive);
    const float y4 = stageSaturate (G * y3 + beta * s[3], drive);

    s[0] = 2.0f * y1 - s[0];
    s[1] = 2.0f * y2 - s[1];
    s[2] = 2.0f * y3 - s[2];
    s[3] = 2.0f * y4 - s[3];
    lastY = y4;

    return y4 * compensation;
}
} // namespace nsynth

