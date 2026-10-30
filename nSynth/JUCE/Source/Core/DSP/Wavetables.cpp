#include "Wavetables.h"
#include "FastMath.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace nsynth
{
namespace
{
constexpr int N = MipWavetable::kTableSize;
constexpr int kFftOrder = 12;
static_assert ((1 << kFftOrder) == N);

// 2048 / sqrt(2)^level rounded down, then every count down to a single harmonic.
constexpr int kLevelHarmonics[MipWavetable::kNumLevels] = { 2048, 1448, 1024, 724, 512, 362, 256, 181, 128, 90, 64,
                                                            45, 32, 22, 16, 11, 8, 6, 4, 3, 2, 1 };

const std::array<double, MipWavetable::kNumLevels> kLevelOctaves = []
{
    std::array<double, MipWavetable::kNumLevels> octaves {};
    for (int level = 0; level < MipWavetable::kNumLevels; ++level)
        octaves[(size_t) level] = std::log2 ((double) kLevelHarmonics[level]);
    return octaves;
}();

float hermite (const std::vector<float>& t, int i, float frac) noexcept
{
    const float y0 = t[(size_t) i], y1 = t[(size_t) i + 1], y2 = t[(size_t) i + 2], y3 = t[(size_t) i + 3];
    // 4-point, 3rd-order Hermite (x-form)
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}

// One alternative cycle as it is saved: peak-normalised 16-bit PCM.
void quantiseCycle (const std::vector<float>& cycle, int16_t* out) noexcept
{
    float peak = 0.0f;
    for (auto value : cycle)
        peak = juce::jmax (peak, std::abs (value));
    const float gain = peak > 1.0e-8f ? 1.0f / peak : 1.0f;
    for (int sample = 0; sample < N; ++sample)
        out[sample] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (cycle[(size_t) sample] * gain * 32767.0f));
}

uint64_t hashCycle (const std::vector<float>& cycle) noexcept
{
    std::array<int16_t, N> pcm {};
    quantiseCycle (cycle, pcm.data());
    uint64_t hash = 14695981039346656037ull; // FNV-1a
    for (auto value : pcm)
    {
        hash ^= (uint16_t) value;
        hash *= 1099511628211ull;
    }
    return hash;
}

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
        inverseToTable (harmonics, kLevelHarmonics[level], levels[(size_t) level]);

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

int MipWavetable::harmonicsAt (int level) noexcept
{
    return kLevelHarmonics[juce::jlimit (0, kNumLevels - 1, level)];
}

double MipWavetable::maxHarmonicFraction (double sampleRate) noexcept
{
    // Below 36.4 kHz nothing may fold back; 0.45 also keeps the top harmonic clear of Nyquist.
    return juce::jlimit (0.45, 0.9, (sampleRate - 20000.0) / sampleRate);
}

MipWavetable::Blend MipWavetable::blendForIncrement (double phaseIncrement, double maxHarmonicFraction) noexcept
{
    const double inc = std::abs (phaseIncrement);
    if (inc <= 0.0)
        return {};

    // The brightest level whose top harmonic stays within the limit. The counts halve every two
    // levels, so the octave estimate lands on it or next to it.
    const double allowedOctaves = fastmath::log2 (maxHarmonicFraction / inc); // log2 of the highest playable harmonic
    int level = juce::jlimit (0, kNumLevels - 1, (int) (2.0 * (kLevelOctaves[0] - allowedOctaves)));
    while (level > 0 && kLevelOctaves[(size_t) level - 1] <= allowedOctaves)
        --level;
    while (level < kNumLevels - 1 && kLevelOctaves[(size_t) level] > allowedOctaves)
        ++level;
    if (level == kNumLevels - 1)
        return { level, 1.0f };

    // Fade to the darker level over the last kFadeOctaves before this one reaches the limit:
    // at the limit the darker level plays alone, exactly what follows once it takes over.
    const double headroom = allowedOctaves - kLevelOctaves[(size_t) level];
    return { level, (float) juce::jmin (1.0, headroom / kFadeOctaves) };
}

float MipWavetable::read (int level, double phase) const noexcept
{
    const double pos = phase * (double) kTableSize;
    const auto i = (int) pos;
    return hermite (levels[(size_t) level], i & (kTableSize - 1), (float) (pos - (double) i));
}

float MipWavetable::read (const Blend& blend, double phase) const noexcept
{
    const double pos = phase * (double) kTableSize;
    const auto i = (int) pos;
    const auto frac = (float) (pos - (double) i);
    const int index = i & (kTableSize - 1);
    const float bright = hermite (levels[(size_t) blend.level], index, frac);
    if (blend.weight >= 1.0f)
        return bright;
    const float dark = hermite (levels[(size_t) blend.level + 1], index, frac);
    return dark + (bright - dark) * blend.weight;
}

//==============================================================================
const juce::StringArray& WaveBank::altWaveNames()
{
    static const juce::StringArray names {
        // 1-8: FM, 4-operator FM waveforms
        "FM Sine", "FM Half Sine", "FM Abs Sine", "FM Quarter Sine", "FM Alt Sine", "FM Camel Sine", "FM Square", "FM Log Saw",
        // 9-16: PD, phase distortion
        "PD Saw", "PD Square", "PD Pulse", "PD Double Sine", "PD Saw Pulse", "PD Reso Saw", "PD Reso Tri", "PD Reso Trap",
        // 17-29: DG, digital waveform spectra
        "DG Piano", "DG E.Piano", "DG Hard EP", "DG Clavi", "DG Organ", "DG Brass", "DG Sax", "DG Strings",
        "DG Guitar", "DG E.Guitar", "DG Bass", "DG Digi Bass", "DG Bell",
        // 30-32: WT, wavetable style
        "WT Vocal A", "WT Vocal O", "WT Digital"
    };
    return names;
}

std::vector<float> WaveBank::makeClassicCycle (int index)
{
    // Analog-modelled single cycles, built through the FFT so every mip level stays band-limited.
    std::vector<float> c ((size_t) N);
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (int n = 0; n < N; ++n)
    {
        const double p = (double) n / (double) N;
        double v = 0.0;
        switch (index)
        {
            case 0: v = std::sin (twoPi * p); break;                               // pure sine (FM reference)
            case 1: v = 2.0 * analogRamp (p) - 1.0; break;                         // analog saw, convex ramp
            case 2:                                                                 // analog square with droop
                v = p < 0.5 ? 1.0 - kSquareDroop * (p / 0.5) : -1.0 + kSquareDroop * ((p - 0.5) / 0.5);
                break;
            case 3:                                                                 // triangle, rounded tips
            {
                const double t = p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
                v = roundTriangle (t);
                break;
            }
            default: break;
        }
        c[(size_t) n] = (float) v;
    }
    return c;
}

std::vector<float> WaveBank::makeFactoryAltCycle (int index)
{
    std::vector<float> c ((size_t) N);
    const double twoPi = juce::MathConstants<double>::twoPi;
    const double pi = juce::MathConstants<double>::pi;

    // Additive spectrum with sine phases.
    auto additive = [&] (double p, int count, auto&& amp)
    {
        double sum = 0.0;
        for (int k = 1; k <= count; ++k)
            sum += amp (k) * std::sin (twoPi * k * p);
        return sum;
    };
    auto peakAt = [] (double k, double centre, double width) { return std::exp (-0.5 * std::pow ((k - centre) / width, 2.0)); };
    // Phase distortion: a cosine read through a bent phase.
    auto pdSaw = [] (double p, double knee) { return p < knee ? 0.5 * p / knee : 0.5 + 0.5 * (p - knee) / (1.0 - knee); };
    auto pdSquare = [] (double p, double knee)
    {
        if (p < knee)        return 0.5 * p / knee;
        if (p < 0.5)         return 0.5;
        if (p < 0.5 + knee)  return 0.5 + 0.5 * (p - 0.5) / knee;
        return 1.0;
    };
    // PD resonance waves: a cosine at the resonance frequency inside a window that restarts every cycle.
    auto reso = [&] (double p, double window) { return 1.0 - window * (1.0 - std::cos (twoPi * 6.0 * p)); };

    for (int n = 0; n < N; ++n)
    {
        const double p = (double) n / (double) N;
        const double s = std::sin (twoPi * p);
        double v = 0.0;

        switch (index)
        {
            // ---- 4-operator FM waveforms
            case 0:  v = s; break;
            case 1:  v = p < 0.5 ? s : 0.0; break;
            case 2:  v = std::abs (s); break;
            case 3:  v = std::fmod (p, 0.5) < 0.25 ? std::abs (s) : 0.0; break;
            case 4:  v = p < 0.5 ? std::sin (2.0 * twoPi * p) : 0.0; break;
            case 5:  v = p < 0.5 ? std::abs (std::sin (2.0 * twoPi * p)) : 0.0; break;
            case 6:  v = p < 0.5 ? 1.0 : -1.0; break;
            case 7:  v = p < 0.5 ? std::exp (-16.0 * p) : -std::exp (-16.0 * (1.0 - p)); break;

            // ---- Phase distortion
            case 8:  v = std::cos (twoPi * pdSaw (p, 0.06)); break;
            case 9:  v = std::cos (twoPi * pdSquare (p, 0.06)); break;
            case 10: v = std::cos (twoPi * juce::jmin (1.0, p / 0.18)); break;
            case 11: v = p < 0.5 ? std::cos (2.0 * twoPi * p) : std::cos (twoPi * (p - 0.5)); break;
            case 12: v = p < 0.5 ? std::cos (twoPi * pdSaw (2.0 * p, 0.12)) : 1.0; break;
            case 13: v = reso (p, 1.0 - p); break;
            case 14: v = reso (p, 1.0 - std::abs (2.0 * p - 1.0)); break;
            case 15: v = reso (p, juce::jmin (1.0, 4.0 * juce::jmin (p, 1.0 - p))); break;

            // ---- Digital spectra
            case 16: v = additive (p, 64, [&] (int k) { return std::pow ((double) k, -0.9) * (k % 7 == 0 ? 0.25 : 1.0); }); break;
            case 17: v = additive (p, 32, [&] (int k) { return (k == 1 ? 1.0 : 0.3 / (k * k)) + 0.12 * peakAt (k, 8.0, 1.0); }); break;
            case 18: v = additive (p, 48, [&] (int k) { return std::pow ((double) k, -1.2) * (1.0 + 1.5 * peakAt (k, 3.0, 0.5) + 2.0 * peakAt (k, 10.0, 1.5)); }); break;
            case 19: v = p < 0.12 ? 1.0 : -0.14; break;
            case 20: v = additive (p, 8, [] (int k) { static const double bars[] = { 0, 1.0, 1.0, 0.8, 0.6, 0, 0.5, 0, 0.4 }; return bars[k]; }); break;
            case 21: v = additive (p, 64, [&] (int k) { return (1.0 / k) * (1.0 + 2.0 * peakAt (k, 5.0, 2.5)); }); break;
            case 22: v = additive (p, 64, [&] (int k) { return (k % 2 == 1 ? 1.0 : 0.4) / k * (1.0 + 1.5 * peakAt (k, 3.0, 1.0) + 1.2 * peakAt (k, 9.0, 2.0)); }); break;
            case 23: v = additive (p, 64, [&] (int k) { return (1.0 / k) * (1.0 + 1.6 * peakAt (k, 4.0, 1.2) + 1.4 * peakAt (k, 11.0, 2.5)) * (1.0 - 0.6 * peakAt (k, 7.0, 0.8)); }); break;
            case 24: v = additive (p, 64, [&] (int k) { return std::abs (std::sin (pi * k * 0.18)) / std::pow ((double) k, 1.3); }); break;
            case 25: v = std::tanh (4.0 * s + 0.6 * std::sin (2.0 * twoPi * p)); break;
            case 26: v = additive (p, 32, [] (int k) { static const double low[] = { 0, 1.0, 0.6, 0.25, 0.15, 0.08 }; return k <= 5 ? low[k] : 0.5 / ((double) k * k * k); }); break;
            case 27: v = additive (p, 15, [] (int k) { return k == 2 ? 0.5 : (k % 2 == 1 ? 1.0 / k : 0.0); }); break;
            case 28: v = additive (p, 16, [] (int k) { return k == 1 ? 1.0 : k == 4 ? 0.7 : k == 7 ? 0.5 : k == 11 ? 0.4 : k == 16 ? 0.25 : 0.0; }); break;

            // ---- Wavetable style
            case 29: v = additive (p, 48, [&] (int k) { return (peakAt (k, 6.5, 1.5) + 0.7 * peakAt (k, 10.0, 2.0) + 0.04) / std::sqrt ((double) k); }); break;
            case 30: v = additive (p, 48, [&] (int k) { return (peakAt (k, 4.0, 1.2) + 0.6 * peakAt (k, 7.0, 1.5) + 0.04) / std::sqrt ((double) k); }); break;
            default: v = std::floor (16.0 * p) / 15.0 * 2.0 - 1.0; break;
        }
        c[(size_t) n] = (float) v;
    }
    return c;
}

WaveBank::WaveBank()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto cycle = makeClassicCycle (i);
        classicTables[(size_t) i].buildFromCycle (cycle.data(), MipWavetable::kTableSize / 2 - 1, false);
    }

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
    altHashes[(size_t) index] = hashCycle (altCycles[(size_t) index]);
}

uint64_t WaveBank::getContentHash() const noexcept
{
    uint64_t hash = 0;
    for (auto slotHash : altHashes)
        hash = (hash ^ slotHash) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
    return hash;
}

std::shared_ptr<const WaveBank> WaveBank::factory()
{
    static const std::shared_ptr<const WaveBank> bank = std::make_shared<const WaveBank>();
    return bank;
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
    std::array<int16_t, N> pcm {};
    for (int wave = 0; wave < kNumAltWaves; ++wave)
    {
        quantiseCycle (altCycles[(size_t) wave], pcm.data());
        for (int sample = 0; sample < N; ++sample)
        {
            const size_t offset = ((size_t) wave * (size_t) N + (size_t) sample) * 2;
            bytes[offset] = (uint8_t) ((uint16_t) pcm[(size_t) sample] & 0xff);
            bytes[offset + 1] = (uint8_t) (((uint16_t) pcm[(size_t) sample] >> 8) & 0xff);
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
