#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace nsynth::test
{
inline double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) juce::jmax ((size_t) 1, x.size() - from));
}

inline float peak (const std::vector<float>& x, size_t from = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < x.size(); ++i)
        p = juce::jmax (p, std::abs (x[i]));
    return p;
}

inline bool allFinite (const std::vector<float>& x)
{
    for (auto v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}

// Frequency from rising zero crossings with linear interpolation.
inline double zeroCrossingFrequency (const std::vector<float>& x, double sampleRate, size_t from = 0)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = juce::jmax ((size_t) 1, from); i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    if (count < 2)
        return 0.0;
    return sampleRate * (double) (count - 1) / (last - first);
}

// Amplitude of a sinusoid at `freq` (Goertzel-style single-bin DFT with a Hann window).
inline double toneAmplitude (const std::vector<float>& x, double freq, double sampleRate, size_t from = 0)
{
    const size_t n = x.size() - from;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) (n - 1));
        const double ph = juce::MathConstants<double>::twoPi * freq * (double) i / sampleRate;
        re += w * x[from + i] * std::cos (ph);
        im -= w * x[from + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

inline double toDb (double v) { return 20.0 * std::log10 (juce::jmax (1.0e-12, v)); }

// Magnitude spectrum (Hann window), size = 2^order, bins 0..N/2.
inline std::vector<double> spectrum (const std::vector<float>& x, int order, size_t from = 0)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) (2 * n), 0.0f);
    for (int i = 0; i < n && from + (size_t) i < x.size(); ++i)
        buf[(size_t) i] = x[from + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    return std::vector<double> (buf.begin(), buf.begin() + n / 2 + 1);
}

inline void setParam (juce::AudioProcessorValueTreeState& s, const juce::String& id, float plainValue)
{
    auto* p = s.getParameter (id);
    jassert (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}
} // namespace nsynth::test
