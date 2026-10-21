#include "TestHelpers.h"
#include "Core/DSP/Envelope.h"

using namespace nsynth;

class EnvelopeTests : public juce::UnitTest
{
public:
    EnvelopeTests() : juce::UnitTest ("Envelopes", "nSynth") {}

    static constexpr double sr = 48000.0;

    static int samplesUntil (Envelope& e, std::function<bool (float)> cond, int limit)
    {
        for (int i = 0; i < limit; ++i)
            if (cond (e.process()))
                return i + 1;
        return -1;
    }

    void runTest() override
    {
        beginTest ("attack time: 0 -> 1 in the set time (1 ms .. 10 s)");
        for (float t : { 0.001f, 0.05f, 1.0f, 10.0f })
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = t; s.sustain = 1.0f;
            e.setSettings (s); e.noteOn();
            const int n = samplesUntil (e, [] (float v) { return v >= 1.0f; }, (int) (sr * 12));
            expectWithinAbsoluteError ((double) n / sr, (double) t, juce::jmax (2.0 / sr, 0.02 * t));
        }

        beginTest ("attack curve is analog-like (RC charging: > 50% at half time)");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = 0.1f; e.setSettings (s); e.noteOn();
            float v = 0.0f;
            for (int i = 0; i < (int) (0.05 * sr); ++i) v = e.process();
            expectGreaterThan (v, 0.6f);
            expectLessThan (v, 0.75f);
        }

        beginTest ("decay: full-scale time, settles exactly on sustain");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = 0.001f; s.decay = 0.5f; s.sustain = 0.0f;
            e.setSettings (s); e.noteOn();
            samplesUntil (e, [] (float v) { return v >= 1.0f; }, 1000);
            const int n = samplesUntil (e, [] (float v) { return v <= 0.0f; }, (int) sr * 2);
            expectWithinAbsoluteError ((double) n / sr, 0.5, 0.01);

            s.sustain = 0.4f; e.setSettings (s); e.noteOn();
            for (int i = 0; i < (int) sr; ++i) e.process();
            expectWithinAbsoluteError (e.getValue(), 0.4f, 1.0e-4f);
            expect (e.getStage() == Envelope::Stage::sustain);
        }

        beginTest ("release: 1 -> 0 in the set time, then idle");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = 0.001f; s.sustain = 1.0f; s.release = 2.0f;
            e.setSettings (s); e.noteOn();
            for (int i = 0; i < 2000; ++i) e.process();
            e.noteOff();
            const int n = samplesUntil (e, [] (float v) { return v <= 0.0f; }, (int) sr * 3);
            expectWithinAbsoluteError ((double) n / sr, 2.0, 0.02);
            expect (! e.isActive());
        }

        beginTest ("ATTACK HOLD delays the attack; DECAY HOLD holds the peak");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attackHold = 0.25f; s.attack = 0.001f; s.decayHold = 0.5f; s.decay = 0.01f; s.sustain = 0.2f;
            e.setSettings (s); e.noteOn();
            const int startOfAttack = samplesUntil (e, [] (float v) { return v > 0.0f; }, (int) sr);
            expectWithinAbsoluteError ((double) startOfAttack / sr, 0.25, 0.002);
            samplesUntil (e, [] (float v) { return v >= 1.0f; }, 1000);
            const int held = samplesUntil (e, [] (float v) { return v < 1.0f; }, (int) sr);
            expectWithinAbsoluteError ((double) held / sr, 0.5, 0.002);
        }

        beginTest ("LOOP repeats attack/decay hold/decay between sustain and peak while the gate is on");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = 0.01f; s.decay = 0.02f; s.sustain = 0.3f; s.loop = true; s.release = 0.05f;
            e.setSettings (s); e.noteOn();
            int peaks = 0;
            float minAfterFirst = 1.0f, prev = 0.0f;
            bool rising = true, firstDecayDone = false;
            for (int i = 0; i < (int) (0.5 * sr); ++i)
            {
                const float v = e.process();
                if (rising && v < prev) { ++peaks; rising = false; }
                if (! rising && v > prev) { rising = true; firstDecayDone = true; }
                if (firstDecayDone) minAfterFirst = juce::jmin (minAfterFirst, v);
                prev = v;
            }
            expectGreaterThan (peaks, 5);
            expectWithinAbsoluteError (minAfterFirst, 0.3f, 0.01f);
            e.noteOff();
            for (int i = 0; i < (int) sr; ++i) e.process();
            expect (! e.isActive());
        }

        beginTest ("retrigger continues from the current level (no click)");
        {
            Envelope e; e.setSampleRate (sr);
            Envelope::Settings s; s.attack = 0.05f; s.sustain = 1.0f; s.release = 0.5f;
            e.setSettings (s); e.noteOn();
            for (int i = 0; i < (int) sr / 10; ++i) e.process();
            e.noteOff();
            for (int i = 0; i < (int) sr / 10; ++i) e.process();
            const float before = e.getValue();
            e.noteOn();
            const float after = e.process();
            expectGreaterThan (after, before);
            expectLessThan (after - before, 0.05f);
        }

        beginTest ("coefficient helper: zero/short times are instantaneous");
        expectEquals (Envelope::coefficientFor (0.0f, 0.3f, sr), 1.0f);
        expectLessThan (Envelope::coefficientFor (1.0f, 0.3f, sr), 1.0f);
    }
};

static EnvelopeTests envelopeTests;
