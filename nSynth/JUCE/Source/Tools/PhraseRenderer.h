#pragma once

// Offline rendering of short musical phrases through the whole plug-in, shared by the factory
// content generator (loudness matching) and nSynthRender (listening tests / analysis).
#include "PluginProcessor.h"

namespace nsynth::tools
{
struct NoteEvent
{
    double startBeat, lengthBeats;
    int note, velocity;
};

inline std::vector<NoteEvent> phraseFor (const juce::String& phrase)
{
    std::vector<NoteEvent> e;
    auto chord = [&] (double at, double len, std::initializer_list<int> notes, int vel)
    {
        for (int n : notes)
            e.push_back ({ at, len, n, vel });
    };

    if (phrase == "bass")
    {
        const int line[] = { 36, 36, 48, 36, 39, 39, 43, 41, 36, 36, 48, 36, 34, 34, 46, 38 };
        for (int i = 0; i < 16; ++i)
            e.push_back ({ i * 0.5, 0.4, line[i], i % 4 == 0 ? 120 : 90 });
        e.push_back ({ 8.0, 3.0, 31, 110 });
    }
    else if (phrase == "lead")
    {
        const int line[] = { 72, 74, 75, 79, 77, 75, 74, 72, 70, 72, 74, 67 };
        const double len[] = { 0.5, 0.5, 1.0, 0.5, 0.5, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 2.0 };
        double t = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            e.push_back ({ t, len[i] * 0.95, line[i], 80 + (i * 13) % 40 });
            t += len[i];
        }
    }
    else if (phrase == "pluck")
    {
        const int arp[] = { 60, 63, 67, 70, 72, 70, 67, 63, 58, 62, 65, 70, 74, 70, 65, 62 };
        for (int i = 0; i < 16; ++i)
            e.push_back ({ i * 0.25, 0.2, arp[i], 70 + (i * 17) % 55 });
        chord (4.5, 1.5, { 48, 60, 63, 67 }, 100);
    }
    else if (phrase == "keys")
    {
        chord (0.0, 0.9, { 48, 60, 63, 67 }, 96);
        chord (1.0, 0.9, { 46, 58, 62, 65 }, 70);
        chord (2.0, 1.9, { 44, 60, 63, 68 }, 110);
        const int line[] = { 72, 75, 79, 77 };
        for (int i = 0; i < 4; ++i)
            e.push_back ({ 4.0 + i * 0.5, 0.45, line[i], 60 + i * 15 });
    }
    else if (phrase == "fx")
    {
        chord (0.0, 3.0, { 48 }, 110);
        chord (4.0, 3.0, { 60, 67 }, 90);
    }
    else // chords
    {
        chord (0.0, 1.9, { 48, 55, 60, 63, 67 }, 96);
        chord (2.0, 1.9, { 44, 51, 60, 63, 68 }, 80);
        chord (4.0, 1.9, { 46, 53, 58, 62, 65 }, 110);
        chord (6.0, 2.5, { 43, 50, 59, 62, 67 }, 64);
    }
    return e;
}

inline juce::String phraseForCategory (const juce::String& category)
{
    const auto c = category.toLowerCase();
    if (c.contains ("bass"))  return "bass";
    if (c.contains ("lead"))  return "lead";
    if (c.contains ("pluck")) return "pluck";
    if (c.contains ("keys"))  return "keys";
    if (c.contains ("fx"))    return "fx";
    return "chords";
}

// Renders the phrase (plus `tailSeconds`) through processBlock at 48 kHz.
inline juce::AudioBuffer<float> renderPhrase (NSynthAudioProcessor& processor, const juce::String& phrase,
                                              double tailSeconds = 4.0, double sampleRate = 48000.0)
{
    constexpr int block = 256;
    constexpr double bpm = 120.0;
    const auto events = phraseFor (phrase);
    double lastBeat = 0.0;
    for (const auto& ev : events)
        lastBeat = juce::jmax (lastBeat, ev.startBeat + ev.lengthBeats);
    const auto beatToSample = [&] (double beat) { return (int64_t) std::llround (beat * 60.0 / bpm * sampleRate); };
    const int64_t total = beatToSample (lastBeat) + (int64_t) (sampleRate * tailSeconds);

    processor.setPlayConfigDetails (0, 2, sampleRate, block);
    processor.prepareToPlay (sampleRate, block);

    juce::AudioBuffer<float> result (2, (int) total);
    result.clear();
    juce::AudioBuffer<float> buffer (2, block);
    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) juce::jmin ((int64_t) block, total - pos);
        juce::MidiBuffer midi;
        for (const auto& ev : events)
        {
            const auto on = beatToSample (ev.startBeat), off = beatToSample (ev.startBeat + ev.lengthBeats);
            if (on >= pos && on < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, ev.note, (juce::uint8) ev.velocity), (int) (on - pos));
            if (off >= pos && off < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, ev.note), (int) (off - pos));
        }
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, n);
        slice.clear();
        processor.processBlock (slice, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, (int) pos, slice, ch, 0, n);
    }
    return result;
}

struct Loudness
{
    double peakDb { -200.0 };
    double momentaryMaxDb { -200.0 };   // loudest 400 ms RMS window (both channels)
};

inline Loudness measure (const juce::AudioBuffer<float>& audio, double sampleRate = 48000.0)
{
    Loudness result;
    const int n = audio.getNumSamples();
    const int window = (int) (0.4 * sampleRate), hop = (int) (0.1 * sampleRate);
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        peak = juce::jmax (peak, audio.getMagnitude (ch, 0, n));
    double loudest = 0.0;
    for (int start = 0; start + window <= n; start += hop)
    {
        double sum = 0.0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* d = audio.getReadPointer (ch, start);
            for (int i = 0; i < window; ++i)
                sum += (double) d[i] * d[i];
        }
        loudest = juce::jmax (loudest, sum / (double) (window * audio.getNumChannels()));
    }
    result.peakDb = juce::Decibels::gainToDecibels ((double) peak, -200.0);
    result.momentaryMaxDb = 10.0 * std::log10 (juce::jmax (1.0e-20, loudest));
    return result;
}
} // namespace nsynth::tools
