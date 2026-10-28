#pragma once

#include "Wavetables.h"
#include "../Params/Enums.h"

namespace nsynth
{
// Information about the centroid core wrapping inside the current sample.
// DDS2 hard sync and the sub oscillator lock to it.
struct WrapEvent
{
    bool wrapped { false };
    float fractionAfter { 0.0f }; // portion of the sample elapsed after the wrap, 0..1
};

// DDS 1: centroid oscillator + six free-running sister oscillators.
class Dds1
{
public:
    static constexpr int kNumCores = 7;
    static constexpr int kCentroid = 3;

    void setWaveBank (const WaveBank* bank) noexcept { waveBank = bank; }
    void setSampleRate (double sr) noexcept { sampleRate = sr; }

    // Random free-running start phases (each physical oscillator has its own).
    void randomisePhases (juce::Random& rng) noexcept;
    void resetPhases() noexcept;

    struct Settings
    {
        Dds1Wave wave { Dds1Wave::saw };
        SuperMode superMode { SuperMode::off };
        float detune { 0.0f };       // PW/DETUNE fader 0..1
        float morph { 0.0f };        // effective PWM/WAVE morph 0..1
        int altA { 0 }, altB { 1 };  // 0-based
        bool mirrorDetune { false }; // right channel of a binaural super voice
    };

    // Called at note-on: SUPER 1/2 resets the DDS1 phase. Otherwise free-running.
    void noteOn (SuperMode mode) noexcept;

    float process (double frequencyHz, const Settings& s, juce::Random& noise, WrapEvent& centroidWrap) noexcept;

    double centroidPhase() const noexcept { return phases[kCentroid]; }

    // Relative frequency offsets of the cores at full detune.
    static float coreOffset (int core) noexcept;
    // Roland JP-8000 super saw detune response (Adam Szabo's measurement): gentle over most of
    // the knob, a steep rise near the top. 0..1 -> 0..1 scale of the core offsets.
    static float superDetuneCurve (float knob) noexcept;
    static void coreGains (SuperMode mode, float& centroidGain, float& sisterGain) noexcept;

private:
    float readWave (Dds1Wave wave, int level, double phase, const Settings& s, float noiseSample) const noexcept;

    const WaveBank* waveBank { nullptr };
    double sampleRate { 44100.0 };
    std::array<double, kNumCores> phases {};
};
} // namespace nsynth
