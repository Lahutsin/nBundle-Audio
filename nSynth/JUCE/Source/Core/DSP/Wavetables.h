#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace nsynth
{
// Single-cycle wavetable with octave mip levels.
// Level j keeps harmonics 1 .. (kTableSize/2 >> j). The oscillator picks the
// lowest level whose highest harmonic stays below kMaxHarmonicFraction of the
// (internal, oversampled) sample rate, so nothing folds back. See docs/03-DSP-SPEC.md §2.1.
class MipWavetable
{
public:
    static constexpr int kTableSize = 4096;      // 4096-point single-cycle .ws6 format
    static constexpr int kNumLevels = 12;        // 2048, 1024, ..., 1 harmonics
    static constexpr int kGuard = 3;             // extra points for 4-point interpolation
    static constexpr float kMaxHarmonicFraction = 0.45f;

    // Builds all mip levels from complex harmonic amplitudes: real = cos part, imag = sin part.
    // harmonics[k] is harmonic k (index 0 = DC, ignored).
    void buildFromHarmonics (const std::vector<std::complex<float>>& harmonics, bool normalisePeak);

    // Builds from one raw cycle of kTableSize samples (FFT -> harmonics -> mip levels).
    void buildFromCycle (const float* cycle, int maxHarmonic, bool normalisePeak);

    static int levelForIncrement (double phaseIncrement) noexcept;

    // phase in [0, 1). Cubic Hermite interpolation.
    float read (int level, double phase) const noexcept;

    const float* levelData (int level) const noexcept { return levels[(size_t) level].data(); }
    int highestHarmonic (int level) const noexcept { return (kTableSize / 2) >> level; }

private:
    std::array<std::vector<float>, kNumLevels> levels;
};

// Owns the classic DDS1 tables (sine/saw/square/triangle) and the 32 alternative
// waveforms (two groups of 16). Built off the audio thread.
class WaveBank
{
public:
    static constexpr int kNumAltWaves = 32;
    static constexpr int kAltMaxHarmonic = MipWavetable::kTableSize / 8; // band limit fs/8

    WaveBank();

    const MipWavetable& classic (int index) const noexcept { return classicTables[(size_t) index]; } // 0 sine..3 triangle
    const MipWavetable& alt (int index) const noexcept { return altTables[(size_t) juce::jlimit (0, kNumAltWaves - 1, index)]; }

    // Replaces one alternative waveform. cycle must hold kTableSize samples.
    void setAltCycle (int index, const float* cycle);
    const std::vector<float>& altCycle (int index) const { return altCycles[(size_t) index]; }

    // .ws6: headerless little-endian int16, 4096 points.
    static bool readWs6 (const juce::File& file, std::vector<float>& cycleOut, juce::String* error = nullptr);
    static bool writeWs6 (const juce::File& file, const std::vector<float>& cycle, juce::String* error = nullptr);
    // WAV/AIFF audio is interpreted as one periodic cycle, downmixed to mono,
    // resampled to kTableSize, DC-removed and peak-normalised.
    static bool readAudioCycle (const juce::File& file, std::vector<float>& cycleOut, juce::String* error = nullptr);
    juce::ValueTree toValueTree() const;
    static bool isValidValueTree (const juce::ValueTree& state, juce::String* error = nullptr);
    bool restoreFromValueTree (const juce::ValueTree& state, juce::String* error = nullptr);

    // nSynth's built-in factory alternative waveforms, modelled on classic digital synths, and their names.
    static std::vector<float> makeFactoryAltCycle (int index);
    static const juce::StringArray& altWaveNames();

    // DDS1 classic cycles (0 sine, 1 saw, 2 square, 3 triangle) with analog-style shapes; DDS2 uses the same shapes.
    static std::vector<float> makeClassicCycle (int index);
    static constexpr double kRampCurve = 0.25;     // Analog saw: slightly convex capacitor ramp
    static constexpr double kSquareDroop = 0.06;   // Analog square: AC-coupled plateau droop
    static double analogRamp (double p) noexcept { return (1.0 - std::exp (-kRampCurve * p)) / (1.0 - std::exp (-kRampCurve)); }
    static double roundTriangle (double t) noexcept { return 1.5 * t - 0.5 * t * t * t; }


private:
    std::array<MipWavetable, 4> classicTables;
    std::array<MipWavetable, kNumAltWaves> altTables;
    std::array<std::vector<float>, kNumAltWaves> altCycles;
};

using WaveBankPtr = std::shared_ptr<const WaveBank>;
} // namespace nsynth
