#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace nsynth
{
enum class LfoShape { triangle, revSaw, sampleHold, square, sine, saw, noise };

// Phase-accumulator LFO used for LFO1 (one per super voice) and LFO2 (one per
// voice, phase-synchronised at note-on).
class Lfo
{
public:
    void setSampleRate (double sr) noexcept { sampleRate = sr; }

    void setPhase (double p) noexcept { phase = p - std::floor (p); finished = false; }
    double getPhase() const noexcept { return phase; }

    // resetPhase: RESET/ONCE modes restart the cycle; once: stop after one cycle.
    void noteOn (bool resetPhase, bool once, double phaseOffset, juce::Random& rng) noexcept;

    float process (double rateHz, LfoShape shape, juce::Random& rng) noexcept;

    static bool isUnipolar (LfoShape s) noexcept { return s == LfoShape::revSaw || s == LfoShape::square || s == LfoShape::saw; }
    static float shapeValue (LfoShape s, double phase, float held) noexcept;

private:
    double sampleRate { 44100.0 };
    double phase { 0.0 };
    bool oneShot { false }, finished { false };
    float held { 0.0f };
};

// Linear fade-in of the LFO depth after note-on (LFO DELAY).
class DelayRamp
{
public:
    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void noteOn() noexcept { level = 0.0f; }
    float process (float delaySeconds) noexcept
    {
        if (delaySeconds <= 0.0f) { level = 1.0f; return level; }
        level = juce::jmin (1.0f, level + (float) (1.0 / ((double) delaySeconds * sampleRate)));
        return level;
    }

private:
    double sampleRate { 44100.0 };
    float level { 1.0f };
};

// Rate and tempo-division tables.
namespace rates
{
    // LF rate fader 0..1 -> 0.05..30 Hz (exponential).
    double lowFrequencyHz (float fader) noexcept;
    // HF mode: 20 Hz..20 kHz.
    double highFrequencyHz (float fader) noexcept;
    // HF TRK: ratio to the played note, +/-4 octaves.
    double hfTrackRatio (float fader) noexcept;
    // DDS2 in LFO range: TUNE -7..+7 -> ~0.1..100 Hz.
    double dds2LfoHz (float tuneSemitones) noexcept;

    // LFO1 synced divisions in beats, slow -> fast.
    const std::array<double, 16>& lfo1SyncBeats() noexcept;
    // Delay division (DelayDivision index, 1/64 .. 1/1) in beats.
    double delayDivisionBeats (int index) noexcept;

    int divisionIndex (float fader, int count) noexcept;
}
} // namespace nsynth
