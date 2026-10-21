#include "Wavetables.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace nsynth
{
namespace
{
constexpr int N = MipWavetable::kTableSize;
constexpr int kFftOrder = 12;
static_assert ((1 << kFftOrder) == N);

void inverseToTable (const std::vector<std::complex<float>>& harmonics, int maxHarmonic, std::vector<float>& out)
{
    juce::dsp::FFT fft (kFftOrder);
    std::vector<juce::dsp::Complex<float>> spec ((size_t) N), time ((size_t) N);

    const int limit = juce::jmin (maxHarmonic, (int) harmonics.size() - 1, N / 2 - 1);
    for (int k = 1; k <= limit; ++k)
    {
        // x[n] = a cos + b sin  <=>  X[k] = N/2 (a - jb), X[N-k] = conj
        const auto h = harmonics[(size_t) k];
        const juce::dsp::Complex<float> X { 0.5f * (float) N * h.real(), -0.5f * (float) N * h.imag() };
        spec[(size_t) k] = X;
        spec[(size_t) (N - k)] = std::conj (X);
    }

    fft.perform (spec.data(), time.data(), true); // JUCE's inverse is scaled by 1/N

    out.assign ((size_t) (N + MipWavetable::kGuard), 0.0f);
    for (int n = 0; n < N; ++n)
        out[(size_t) n + 1] = time[(size_t) n].real();
    out[0] = out[(size_t) N];
    out[(size_t) N + 1] = out[1];
    out[(size_t) N + 2] = out[2];
}
} // namespace

void MipWavetable::buildFromHarmonics (const std::vector<std::complex<float>>& harmonics, bool normalisePeak)
{
    for (int level = 0; level < kNumLevels; ++level)
        inverseToTable (harmonics, (kTableSize / 2) >> level, levels[(size_t) level]);

    if (normalisePeak)
    {
        float peak = 0.0f;
        for (auto v : levels[0])
            peak = juce::jmax (peak, std::abs (v));

        if (peak > 1.0e-9f)
            for (auto& level : levels)
                for (auto& v : level)
                    v /= peak;
    }
}

void MipWavetable::buildFromCycle (const float* cycle, int maxHarmonic, bool normalisePeak)
{
    juce::dsp::FFT fft (kFftOrder);
    std::vector<juce::dsp::Complex<float>> time ((size_t) N), spec ((size_t) N);
    for (int n = 0; n < N; ++n)
        time[(size_t) n] = { cycle[n], 0.0f };

    fft.perform (time.data(), spec.data(), false);

    std::vector<std::complex<float>> harmonics ((size_t) (N / 2), { 0.0f, 0.0f });
    const int limit = juce::jmin (maxHarmonic, N / 2 - 1);
    for (int k = 1; k <= limit; ++k)
    {
        const auto X = spec[(size_t) k];
        harmonics[(size_t) k] = { 2.0f * X.real() / (float) N, -2.0f * X.imag() / (float) N };
    }

    buildFromHarmonics (harmonics, normalisePeak);
}

int MipWavetable::levelForIncrement (double phaseIncrement) noexcept
{
    const double inc = std::abs (phaseIncrement);
    if (inc <= 0.0)
        return 0;

    const double needed = (double) (kTableSize / 2) * inc / (double) kMaxHarmonicFraction;
    if (needed <= 1.0)
        return 0;

    int ilog = 0;
    const double normalized = std::frexp (needed, &ilog);
    const int level = normalized == 0.5 ? (ilog - 1) : ilog;
    return juce::jlimit (0, kNumLevels - 1, level);
}

float MipWavetable::read (int level, double phase) const noexcept
{
    const auto& t = levels[(size_t) level];
    const double pos = phase * (double) kTableSize;
    auto i = (int) pos;
    const auto frac = (float) (pos - (double) i);
    i &= (kTableSize - 1);

    const float y0 = t[(size_t) i], y1 = t[(size_t) i + 1], y2 = t[(size_t) i + 2], y3 = t[(size_t) i + 3];
    // 4-point, 3rd-order Hermite (x-form)
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}

//==============================================================================
std::vector<std::complex<float>> WaveBank::classicHarmonics (int index)
{
    std::vector<std::complex<float>> h ((size_t) (N / 2), { 0.0f, 0.0f });
    const float pi = juce::MathConstants<float>::pi;

    for (int k = 1; k < N / 2; ++k)
    {
        const auto kf = (float) k;
        switch (index)
        {
            case 0: if (k == 1) h[1] = { 0.0f, 1.0f }; break;                      // sine
            case 1: h[(size_t) k] = { 0.0f, -2.0f / (pi * kf) }; break;            // rising saw 2p-1
            case 2: if (k % 2 == 1) h[(size_t) k] = { 0.0f, 4.0f / (pi * kf) }; break;  // square, +1 first half
            case 3:                                                                 // triangle, 0 -> +1 -> -1 -> 0
                if (k % 2 == 1)
                {
                    const float sign = ((k - 1) / 2) % 2 == 0 ? 1.0f : -1.0f;
                    h[(size_t) k] = { 0.0f, sign * 8.0f / (pi * pi * kf * kf) };
                }
                break;
            default: break;
        }
    }
    return h;
}

std::vector<float> WaveBank::makeFactoryAltCycle (int index)
{
    std::vector<float> c ((size_t) N);
    const double twoPi = juce::MathConstants<double>::twoPi;
    juce::Random rng (0x5eed + index);
    std::vector<double> randomAmps (129);
    for (auto& a : randomAmps)
        a = rng.nextDouble();

    auto harmonicSum = [&] (double p, int count, auto&& amp)
    {
        double s = 0.0;
        for (int k = 1; k <= count; ++k)
            s += amp (k) * std::sin (twoPi * k * p);
        return s;
    };

    auto formant = [&] (double p, double f1, double f2)
    {
        return harmonicSum (p, 64, [&] (int k)
        {
            const double g1 = std::exp (-0.5 * std::pow ((k - f1) / 1.5, 2.0));
            const double g2 = 0.6 * std::exp (-0.5 * std::pow ((k - f2) / 2.5, 2.0));
            return (g1 + g2 + 0.05) / std::sqrt ((double) k);
        });
    };

    for (int n = 0; n < N; ++n)
    {
        const double p = (double) n / (double) N;
        const double s = std::sin (twoPi * p);
        double v = 0.0;

        switch (index)
        {
            case 0:  v = p < 0.25 ? 1.0 : -1.0; break;
            case 1:  v = p < 0.125 ? 1.0 : -1.0; break;
            case 2:  v = p < 0.06 ? 1.0 : -1.0; break;
            case 3:  v = 2.0 * std::abs (std::sin (juce::MathConstants<double>::pi * p)) - 1.0; break;
            case 4:  v = s + 0.6 * std::sin (2 * twoPi * p) + 0.4 * std::sin (3 * twoPi * p); break;
            case 5:  v = harmonicSum (p, 8, [] (int k) { return (k == 1 || k == 2 || k == 3 || k == 4 || k == 6 || k == 8) ? 1.0 : 0.0; }); break;
            case 6:  v = formant (p, 7.0, 12.0); break;
            case 7:  v = formant (p, 3.0, 8.0); break;
            case 8:  v = formant (p, 3.0, 25.0); break;
            case 9:  v = harmonicSum (p, 16, [] (int k) { return k == 1 ? 1.0 : k == 4 ? 0.7 : k == 7 ? 0.5 : k == 11 ? 0.4 : k == 16 ? 0.25 : 0.0; }); break;
            case 10: v = harmonicSum (p, 7, [] (int k) { return k % 2 == 1 ? 1.0 : 0.0; }); break;
            case 11: v = harmonicSum (p, 64, [] (int k) { return 1.0 / std::pow ((double) k, 0.3); }); break;
            case 12: v = 2.0 * std::fmod (1.5 * p, 1.0) - 1.0; break;
            case 13: v = 2.0 * std::fmod (2.5 * p, 1.0) - 1.0; break;
            case 14: v = 2.0 * std::fmod (3.7 * p, 1.0) - 1.0; break;
            case 15: v = std::sin (twoPi * p + 2.0 * s); break;
            case 16: v = std::sin (twoPi * p + 3.0 * std::sin (2 * twoPi * p)); break;
            case 17: v = std::sin (twoPi * p + 1.5 * std::sin (3 * twoPi * p)); break;
            case 18: v = std::sin (2 * twoPi * p + 2.0 * s); break;
            case 19: v = std::sin (juce::MathConstants<double>::pi * 1.5 * s); break;
            case 20: v = std::sin (juce::MathConstants<double>::pi * 3.0 * s); break;
            case 21: v = std::tanh (4.0 * s); break;
            case 22: v = (s >= 0 ? 1.0 : -1.0) * std::pow (std::abs (s), 0.3); break;
            case 23: v = std::pow (s, 5.0); break;
            case 24: v = std::floor (4.0 * p) / 3.0 * 2.0 - 1.0; break;
            case 25: v = std::floor (8.0 * p) / 7.0 * 2.0 - 1.0; break;
            case 26: v = std::round (s * 3.5) / 3.5; break;
            case 27: v = std::pow (2.0 * p - 1.0, 2.0) * 2.0 - 1.0; break;
            case 28: v = (2.0 * p - 1.0) + 0.5 * s; break;
            case 29: v = p < 0.8 ? (2.0 * p / 0.8 - 1.0) : (1.0 - 2.0 * (p - 0.8) / 0.2); break;
            case 30: v = harmonicSum (p, 32, [&] (int k) { return randomAmps[(size_t) k] / k; }); break;
            default: v = harmonicSum (p, 128, [&] (int k) { return randomAmps[(size_t) k] / std::sqrt ((double) k); }); break;
        }
        c[(size_t) n] = (float) v;
    }
    return c;
}

WaveBank::WaveBank()
{
    for (int i = 0; i < 4; ++i)
        classicTables[(size_t) i].buildFromHarmonics (classicHarmonics (i), false);

    for (int i = 0; i < kNumAltWaves; ++i)
    {
        auto cycle = makeFactoryAltCycle (i);
        setAltCycle (i, cycle.data());
    }
}

void WaveBank::setAltCycle (int index, const float* cycle)
{
    jassert (juce::isPositiveAndBelow (index, kNumAltWaves));
    altTables[(size_t) index].buildFromCycle (cycle, kAltMaxHarmonic, true);
    altCycles[(size_t) index].assign (cycle, cycle + N);
}

bool WaveBank::readWs6 (const juce::File& file, std::vector<float>& cycleOut, juce::String* error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() != (size_t) N * 2)
    {
        if (error) *error = "A .ws6 file must contain exactly 8192 bytes (4096 x int16).";
        return false;
    }

    cycleOut.resize ((size_t) N);
    auto* bytes = static_cast<const uint8_t*> (data.getData());
    for (int n = 0; n < N; ++n)
    {
        const auto raw = (int16_t) (uint16_t) (bytes[2 * n] | (bytes[2 * n + 1] << 8));
        cycleOut[(size_t) n] = (float) raw / 32768.0f;
    }
    return true;
}

bool WaveBank::writeWs6 (const juce::File& file, const std::vector<float>& cycle, juce::String* error)
{
    if ((int) cycle.size() != N)
    {
        if (error) *error = "Cycle must contain 4096 samples.";
        return false;
    }

    juce::MemoryBlock data ((size_t) N * 2);
    auto* bytes = static_cast<uint8_t*> (data.getData());
    for (int n = 0; n < N; ++n)
    {
        const auto v = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (cycle[(size_t) n] * 32767.0f));
        bytes[2 * n] = (uint8_t) ((uint16_t) v & 0xff);
        bytes[2 * n + 1] = (uint8_t) (((uint16_t) v >> 8) & 0xff);
    }

    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        if (error) *error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool WaveBank::readAudioCycle (const juce::File& file, std::vector<float>& cycleOut, juce::String* error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        if (error) *error = "Could not decode audio file: " + file.getFullPathName();
        return false;
    }

    constexpr int64_t maxFrames = 131072;
    if (reader->lengthInSamples < 2 || reader->lengthInSamples > maxFrames
        || reader->numChannels < 1 || reader->numChannels > 64
        || reader->lengthInSamples > maxFrames / reader->numChannels)
    {
        if (error) *error = "Audio cycle must contain 2..131072 frames and 1..64 channels.";
        return false;
    }

    const int frameCount = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio ((int) reader->numChannels, frameCount);
    if (! reader->read (&audio, 0, frameCount, 0, true, true))
    {
        if (error) *error = "Could not read audio samples from " + file.getFullPathName();
        return false;
    }

    std::vector<float> source ((size_t) frameCount, 0.0f);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = 0; i < frameCount; ++i)
            source[(size_t) i] += audio.getSample (ch, i) / (float) audio.getNumChannels();

    const int maxHarmonic = juce::jmin (kAltMaxHarmonic, frameCount / 2);
    std::vector<std::complex<double>> harmonics ((size_t) (maxHarmonic + 1));
    for (int k = 1; k <= maxHarmonic; ++k)
    {
        if ((frameCount % 2) == 0 && k == frameCount / 2)
        {
            double nyquist = 0.0;
            for (int n = 0; n < frameCount; ++n)
                nyquist += (n % 2 == 0 ? 1.0 : -1.0) * source[(size_t) n];
            harmonics[(size_t) k] = { nyquist / (double) frameCount, 0.0 };
            continue;
        }

        const double omega = juce::MathConstants<double>::twoPi * (double) k / (double) frameCount;
        const double cosine = std::cos (omega), sine = std::sin (omega);
        const double recurrence = 2.0 * cosine;
        // Goertzel extracts the periodic source's exact bin without aliasing high partials during resampling.
        double previous = 0.0, beforePrevious = 0.0;
        for (auto sample : source)
        {
            const double current = (double) sample + recurrence * previous - beforePrevious;
            beforePrevious = previous;
            previous = current;
        }

        const double rawReal = previous - beforePrevious * cosine;
        const double rawImaginary = beforePrevious * sine;
        const std::complex<double> dft {
            rawReal * cosine + rawImaginary * sine,
            rawImaginary * cosine - rawReal * sine
        };
        harmonics[(size_t) k] = dft * (2.0 / (double) frameCount);
    }

    cycleOut.resize ((size_t) N);
    double mean = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * (double) i / (double) N;
        const double cosineStep = std::cos (phase), sineStep = std::sin (phase);
        double cosineHarmonic = cosineStep, sineHarmonic = sineStep;
        double value = 0.0;
        for (int k = 1; k <= maxHarmonic; ++k)
        {
            const auto coefficient = harmonics[(size_t) k];
            value += coefficient.real() * cosineHarmonic - coefficient.imag() * sineHarmonic;
            const double nextCosine = cosineHarmonic * cosineStep - sineHarmonic * sineStep;
            sineHarmonic = sineHarmonic * cosineStep + cosineHarmonic * sineStep;
            cosineHarmonic = nextCosine;
        }
        cycleOut[(size_t) i] = std::isfinite (value) ? (float) value : 0.0f;
        mean += cycleOut[(size_t) i];
    }

    mean /= (double) N;
    float peak = 0.0f;
    for (auto& value : cycleOut)
    {
        value -= (float) mean;
        peak = juce::jmax (peak, std::abs (value));
    }

    if (peak < 1.0e-8f)
    {
        cycleOut.clear();
        if (error) *error = "Audio cycle is silent or too quiet to normalise.";
        return false;
    }

    for (auto& value : cycleOut)
        value /= peak;
    return true;
}

juce::ValueTree WaveBank::toValueTree() const
{
    juce::MemoryBlock cycles ((size_t) kNumAltWaves * (size_t) N * 2);
    auto* bytes = static_cast<uint8_t*> (cycles.getData());
    for (int wave = 0; wave < kNumAltWaves; ++wave)
    {
        float peak = 0.0f;
        for (auto value : altCycles[(size_t) wave])
            peak = juce::jmax (peak, std::abs (value));
        const float gain = peak > 1.0e-8f ? 1.0f / peak : 1.0f;
        for (int sample = 0; sample < N; ++sample)
        {
            const float value = altCycles[(size_t) wave][(size_t) sample] * gain;
            const auto pcm = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (value * 32767.0f));
            const size_t offset = ((size_t) wave * (size_t) N + (size_t) sample) * 2;
            bytes[offset] = (uint8_t) ((uint16_t) pcm & 0xff);
            bytes[offset + 1] = (uint8_t) (((uint16_t) pcm >> 8) & 0xff);
        }
    }

    juce::ValueTree state ("USER_WAVES");
    state.setProperty ("schemaVersion", 1, nullptr);
    state.setProperty ("cycles", juce::var (cycles), nullptr);
    return state;
}

bool WaveBank::isValidValueTree (const juce::ValueTree& state, juce::String* error)
{
    const auto encodedCycles = state.getProperty ("cycles");
    const auto* data = encodedCycles.getBinaryData();
    const size_t expectedSize = (size_t) kNumAltWaves * (size_t) N * 2;
    if (! state.hasType ("USER_WAVES") || (int) state.getProperty ("schemaVersion", 0) != 1
        || data == nullptr || data->getSize() != expectedSize)
    {
        if (error) *error = "User wavetable state has an unsupported schema or invalid payload size.";
        return false;
    }
    return true;
}

bool WaveBank::restoreFromValueTree (const juce::ValueTree& state, juce::String* error)
{
    if (! isValidValueTree (state, error))
        return false;
    const auto encodedCycles = state.getProperty ("cycles");
    const auto* data = encodedCycles.getBinaryData();

    const auto* bytes = static_cast<const uint8_t*> (data->getData());
    std::array<std::vector<float>, kNumAltWaves> staged;
    for (int wave = 0; wave < kNumAltWaves; ++wave)
    {
        auto& cycle = staged[(size_t) wave];
        cycle.resize ((size_t) N);
        for (int sample = 0; sample < N; ++sample)
        {
            const size_t offset = ((size_t) wave * (size_t) N + (size_t) sample) * 2;
            const auto raw = (int16_t) (uint16_t) (bytes[offset] | (bytes[offset + 1] << 8));
            cycle[(size_t) sample] = (float) raw / 32768.0f;
        }
    }

    for (int wave = 0; wave < kNumAltWaves; ++wave)
        setAltCycle (wave, staged[(size_t) wave].data());
    return true;
}
} // namespace nsynth
