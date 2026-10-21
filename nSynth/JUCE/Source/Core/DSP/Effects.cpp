#include "Effects.h"

namespace nsynth
{
StereoChorus::ModeSpec StereoChorus::specFor (ChorusMode mode) noexcept
{
    switch (mode)
    {
        case ChorusMode::one:    return { 0.513, 1.66, 5.35 };
        case ChorusMode::two:    return { 0.863, 1.66, 5.35 };
        case ChorusMode::oneTwo: return { 9.75, 3.3, 3.7 };
        case ChorusMode::off:    break;
    }
    return { 0.0, 3.5, 3.5 };
}

void StereoChorus::prepare (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) std::ceil (sr * 0.02) + 4);
    lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 9000.0 / sr));
    reset();
}

void StereoChorus::reset()
{
    line.clear();
    writePos = 0;
    phase = 0.0;
    wetGain = 0.0f;
    lpState[0] = lpState[1] = 0.0f;
}

float StereoChorus::readDelay (int channel, double delaySamples) const noexcept
{
    const int size = line.getNumSamples();
    double readPos = (double) writePos - delaySamples;
    while (readPos < 0.0)
        readPos += size;
    const int i0 = (int) readPos;
    const float frac = (float) (readPos - i0);
    const float* d = line.getReadPointer (channel);
    const float a = d[i0 % size], b = d[(i0 + 1) % size];
    return a + (b - a) * frac;
}

void StereoChorus::process (juce::AudioBuffer<float>& buffer, int start, int num, ChorusMode mode) noexcept
{
    if (mode == ChorusMode::off && wetGain < 1.0e-5f)
    {
        wetGain = 0.0f;
        return;
    }

    const auto spec = specFor (mode);
    const float targetWet = mode == ChorusMode::off ? 0.0f : 1.0f;
    const double centre = 0.5 * (spec.minDelayMs + spec.maxDelayMs) * 0.001 * sr;
    const double depth = 0.5 * (spec.maxDelayMs - spec.minDelayMs) * 0.001 * sr;
    const int size = line.getNumSamples();

    for (int i = start; i < start + num; ++i)
    {
        const float inL = buffer.getSample (0, i), inR = buffer.getSample (1, i);
        line.setSample (0, writePos, inL);
        line.setSample (1, writePos, inR);

        // Smooth BBD LFO (75% triangle + 25% sine) for organic modulation without turnaround corners,
        // opposite polarity on the right channel.
        const double tri = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
        const double lfoVal = 0.75 * tri + 0.25 * std::sin (juce::MathConstants<double>::twoPi * phase);
        phase += spec.rateHz / sr;
        phase -= std::floor (phase);

        float wetL = readDelay (0, centre + depth * lfoVal);
        float wetR = readDelay (1, centre - depth * lfoVal);
        // Subtle BBD bucket soft-saturation before anti-imaging lowpass
        wetL = wetL * (1.0f - 0.03f * wetL * wetL / (1.0f + wetL * wetL));
        wetR = wetR * (1.0f - 0.03f * wetR * wetR / (1.0f + wetR * wetR));
        lpState[0] += (wetL - lpState[0]) * lpCoef;
        lpState[1] += (wetR - lpState[1]) * lpCoef;

        wetGain += (targetWet - wetGain) * 0.001f;
        // Equal dry/wet sum, level-normalised.
        buffer.setSample (0, i, inL * (1.0f - 0.5f * wetGain) + 0.5f * wetGain * lpState[0]);
        buffer.setSample (1, i, inR * (1.0f - 0.5f * wetGain) + 0.5f * wetGain * lpState[1]);

        writePos = (writePos + 1) % size;
    }
}

//==============================================================================
void StereoDelay::prepare (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) std::ceil (sr * kMaxSeconds) + 4);
    reset();
}

void StereoDelay::reset()
{
    line.clear();
    writePos = 0;
    currentDelaySamples = -1.0;
    smoothedSend = -1.0f; // initialised to the first target
}

void StereoDelay::process (juce::AudioBuffer<float>& buffer, int start, int num,
                           double timeSeconds, float send, float feedback, bool freeze) noexcept
{
    const int size = line.getNumSamples();
    const double target = juce::jlimit (1.0, (double) size - 3.0, timeSeconds * sr);
    if (currentDelaySamples < 0.0)
        currentDelaySamples = target;

    const float fb = freeze ? 1.0f : juce::jlimit (0.0f, 1.0f, feedback);
    const float sendTarget = freeze ? 0.0f : juce::jlimit (0.0f, 1.0f, send);
    const double slew = 1.0 - std::exp (-1.0 / (0.05 * sr)); // ~50 ms tape-like time glide
    if (smoothedSend < 0.0f || freeze)
        smoothedSend = sendTarget; // freeze stops recording immediately

    for (int i = start; i < start + num; ++i)
    {
        currentDelaySamples += (target - currentDelaySamples) * slew;
        smoothedSend += (sendTarget - smoothedSend) * 0.002f;

        double readPos = (double) writePos - currentDelaySamples;
        while (readPos < 0.0)
            readPos += size;
        int i0 = (int) readPos;
        float frac = (float) (readPos - i0);

        // Infinite feedback must not degrade: interpolation would low-pass
        // every pass, so read at an integer position in that case.
        if (fb >= 0.999f)
        {
            i0 = (int) std::lround (readPos);
            frac = 0.0f;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = line.getReadPointer (ch);
            const float delayed = d[i0 % size] + (d[(i0 + 1) % size] - d[i0 % size]) * frac;
            const float dry = buffer.getSample (ch, i);
            line.setSample (ch, writePos, smoothedSend * dry + fb * delayed);
            buffer.setSample (ch, i, dry + delayed);
        }
        writePos = (writePos + 1) % size;
    }
}
} // namespace nsynth
