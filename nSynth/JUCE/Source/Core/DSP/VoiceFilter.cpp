#include "VoiceFilter.h"

namespace nsynth
{
void VoiceFilter::setSampleRate (double sr) noexcept
{
    sampleRate = sr;
    lastHpfHz = lastLpfHz = lastRes = -1.0f;
}

float VoiceFilter::softClip (float x) noexcept
{
    if (x >= 3.0f)  return 1.0f;
    if (x <= -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float VoiceFilter::softClipRatio (float x) noexcept
{
    const float a = std::abs (x);
    if (a < 1.0e-6f)
        return 1.0f;
    if (a >= 3.0f)
        return 1.0f / a;
    const float x2 = x * x;
    return (27.0f + x2) / (27.0f + 9.0f * x2);
}

float VoiceFilter::prewarp (float hz, double sr) noexcept
{
    const double f = juce::jlimit (1.0, 0.49 * sr, (double) hz);
    return (float) std::tan (juce::MathConstants<double>::pi * f / sr);
}

void VoiceFilter::reset() noexcept
{
    hpState = 0.0f;
    for (int i = 0; i < 4; ++i)
        s[i] = y[i] = 0.0f;
    lastInput = 0.0f;
}

void VoiceFilter::setParameters (float hpfHz, float lpfHz, float res, Drive newDrive, FilterType newType) noexcept
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
        g = prewarp (lpfHz, sampleRate);
    }

    if (std::abs (res - lastRes) > 1.0e-6f || drive != newDrive || type != newType)
    {
        lastRes = res;
        if (type != newType)
            for (auto& v : y)
                v = 0.0f;
        type = newType;
        drive = newDrive;
        const float r = juce::jlimit (0.0f, 1.0f, res);
        k = r * kMaxFeedback;
        // SEM: damping 2R from Butterworth (R = 0.707) down to slightly negative for self-oscillation.
        svfDamping = 2.0f * (0.7071f - 0.7871f * r);

        // DRIVE sets how hard the input hits the saturating stages; the output gain keeps small
        // signals at unity (OFF) or a little louder (1, 2) while loud signals compress and thicken.
        switch (drive)
        {
            case Drive::off: inputGain = 0.7f; outputGain = 1.0f / 0.7f; break;
            case Drive::one: inputGain = 1.8f; outputGain = 0.85f;       break;
            case Drive::two: inputGain = 4.5f; outputGain = 0.42f;       break;
        }

        // A ladder loses 1 / (1 + k) of its passband with resonance. Half of it is made up at the
        // input, which keeps the low end fat and drives the stages harder, as on a Minimoog.
        switch (type)
        {
            case FilterType::moog24:
            case FilterType::moog12:    bassCompensation = 0.5f; break;
            case FilterType::jupiter24: bassCompensation = 0.6f; break;
            case FilterType::sem12:     bassCompensation = 0.0f; break;
        }
    }
}

float VoiceFilter::processLadder (float u0) noexcept
{
    const bool ota = type == FilterType::jupiter24;
    float u = lastInput;
    float est[4] { y[0], y[1], y[2], y[3] };

    float a[4], b[4];
    for (int pass = 0; pass < 2; ++pass)
    {
        // Linearised stage coefficients around the current estimate: y_i = a_i * y_(i-1) + b_i.
        const float tIn = softClipRatio (u);
        for (int i = 0; i < 4; ++i)
        {
            float c, d;
            if (ota)
            {
                // OTA: dy_i/dt = g * tanh (y_(i-1) - y_i), the input is pre-saturated.
                const float prev = i == 0 ? tIn * u : est[i - 1];
                const float t = softClipRatio (prev - est[i]);
                c = i == 0 ? t * tIn : t;
                d = t;
            }
            else
            {
                // Transistor ladder: dy_i/dt = g * (tanh (y_(i-1)) - tanh (y_i)), y_0 = u.
                c = i == 0 ? tIn : softClipRatio (est[i - 1]);
                d = softClipRatio (est[i]);
            }
            const float denominator = 1.0f / (1.0f + g * d);
            a[i] = g * c * denominator;
            b[i] = s[i] * denominator;
        }

        const float A = a[0] * a[1] * a[2] * a[3];
        const float B = ((b[0] * a[1] + b[1]) * a[2] + b[2]) * a[3] + b[3];
        const float y4 = (A * u0 + B) / (1.0f + k * A);
        u = u0 - k * y4;
        est[0] = a[0] * u + b[0];
        est[1] = a[1] * est[0] + b[1];
        est[2] = a[2] * est[1] + b[2];
        est[3] = y4;
    }

    for (int i = 0; i < 4; ++i)
    {
        y[i] = est[i];
        s[i] = 2.0f * est[i] - s[i];
    }
    lastInput = u;
    return type == FilterType::moog12 ? y[1] : y[3];
}

float VoiceFilter::processSvf (float x) noexcept
{
    // TPT state-variable filter; the band-pass integrator saturates, which bounds the
    // self-oscillation and gives the SEM its round, vocal resonance.
    const float hp = (x - (svfDamping + g) * s[0] - s[1]) / (1.0f + svfDamping * g + g * g);
    const float bp = g * hp + s[0];
    const float lp = g * bp + s[1];
    s[0] = 2.0f * softClip (bp - 0.5f * s[0]);
    s[1] = 2.0f * lp - s[1];
    return lp;
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
    const float hp = x - lp;

    if (type == FilterType::sem12)
        return processSvf (softClip (hp * inputGain)) * outputGain;

    const float u0 = hp * inputGain * (1.0f + bassCompensation * k);
    return processLadder (u0) * outputGain;
}
} // namespace nsynth
