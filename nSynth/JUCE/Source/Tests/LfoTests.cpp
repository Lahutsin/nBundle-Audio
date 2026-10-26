#include "TestHelpers.h"
#include "Core/DSP/Lfo.h"
#include "Core/Params/Enums.h"

using namespace nsynth;
using namespace nsynth::test;

class LfoTests : public juce::UnitTest
{
public:
    LfoTests() : juce::UnitTest ("LFOs", "nSynth") {}

    static constexpr double sr = 48000.0;

    std::vector<float> render (Lfo& l, double hz, LfoShape s, int n, juce::Random& rng)
    {
        std::vector<float> out ((size_t) n);
        for (auto& v : out) v = l.process (hz, s, rng);
        return out;
    }

    void runTest() override
    {
        juce::Random rng (7);

        beginTest ("rate accuracy");
        {
            Lfo l; l.setSampleRate (sr);
            auto x = render (l, 2.0, LfoShape::sine, (int) sr * 10, rng);
            expectWithinAbsoluteError (zeroCrossingFrequency (x, sr), 2.0, 0.002);
        }

        beginTest ("polarity: triangle/sine/S&H bipolar, rev saw/square/saw unipolar positive");
        for (auto shape : { LfoShape::triangle, LfoShape::sine, LfoShape::sampleHold, LfoShape::revSaw, LfoShape::square, LfoShape::saw })
        {
            Lfo l; l.setSampleRate (sr);
            auto x = render (l, 5.0, shape, (int) sr * 4, rng);
            const auto [mn, mx] = std::minmax_element (x.begin(), x.end());
            if (Lfo::isUnipolar (shape))
            {
                expectGreaterOrEqual (*mn, 0.0f);
                expectWithinAbsoluteError (*mx, 1.0f, 0.01f);
            }
            else
            {
                expectLessThan (*mn, -0.5f);
                expectGreaterThan (*mx, 0.5f);
            }
        }

        beginTest ("triangle starts at zero and rises; rev saw starts at 1");
        expectWithinAbsoluteError (Lfo::shapeValue (LfoShape::triangle, 0.0, 0), 0.0f, 1e-6f);
        expectWithinAbsoluteError (Lfo::shapeValue (LfoShape::triangle, 0.25, 0), 1.0f, 1e-6f);
        expectWithinAbsoluteError (Lfo::shapeValue (LfoShape::revSaw, 0.0, 0), 1.0f, 1e-6f);

        beginTest ("sample & hold changes exactly once per cycle");
        {
            Lfo l; l.setSampleRate (sr);
            auto x = render (l, 10.0, LfoShape::sampleHold, (int) sr * 2, rng);
            int changes = 0;
            for (size_t i = 1; i < x.size(); ++i) changes += juce::exactlyEqual (x[i], x[i - 1]) ? 0 : 1;
            expectWithinAbsoluteError (changes, 20, 1);
        }

        beginTest ("ONCE runs a single cycle and stops");
        {
            Lfo l; l.setSampleRate (sr);
            l.noteOn (true, true, 0.0, rng);
            auto x = render (l, 4.0, LfoShape::revSaw, (int) sr, rng); // 4 cycles worth of time
            expectWithinAbsoluteError (x.front(), 1.0f, 1e-3f);
            const size_t afterOneCycle = (size_t) (sr / 4.0) + 2;
            for (size_t i = afterOneCycle; i < x.size(); ++i)
                expectEquals (x[i], 0.0f);
        }

        beginTest ("RESET restarts the phase on note-on, FREE keeps running");
        {
            Lfo l; l.setSampleRate (sr);
            render (l, 3.0, LfoShape::triangle, 12345, rng);
            l.noteOn (true, false, 0.0, rng);
            expectWithinAbsoluteError (l.getPhase(), 0.0, 1e-12);
            render (l, 3.0, LfoShape::triangle, 777, rng);
            const double before = l.getPhase();
            l.noteOn (false, false, 0.0, rng);
            expectWithinAbsoluteError (l.getPhase(), before, 1e-12);
        }

        beginTest ("rate tables");
        expectWithinAbsoluteError (rates::lowFrequencyHz (0.0f), 0.05, 1e-9);
        expectWithinAbsoluteError (rates::lowFrequencyHz (1.0f), 30.0, 1e-6);
        expectWithinAbsoluteError (rates::highFrequencyHz (0.0f), 20.0, 1e-9);
        expectWithinAbsoluteError (rates::highFrequencyHz (1.0f), 20000.0, 1e-6);
        expectWithinAbsoluteError (rates::hfTrackRatio (0.5f), 1.0, 1e-9);
        {
            const auto& lfo = rates::lfo1SyncBeats();
            expectEquals (lfo.front(), 32.0);
            expectEquals (lfo[7], 1.0);
            expectWithinAbsoluteError (lfo.back(), 1.0 / 6.0, 1e-12);
            for (size_t i = 1; i < lfo.size(); ++i) expectLessThan (lfo[i], lfo[i - 1]);
            const int divisions = toInt (DelayDivision::d1_1) + 1;
            expectEquals (rates::delayDivisionBeats (toInt (DelayDivision::d1_64)), 1.0 / 16.0);
            expectEquals (rates::delayDivisionBeats (toInt (DelayDivision::d1_4)), 1.0);
            expectEquals (rates::delayDivisionBeats (toInt (DelayDivision::d1_8D)), 0.75);
            expectWithinAbsoluteError (rates::delayDivisionBeats (toInt (DelayDivision::d1_8T)), 1.0 / 3.0, 1e-12);
            expectEquals (rates::delayDivisionBeats (toInt (DelayDivision::d1_1)), 4.0);
            for (int i = 1; i < divisions; ++i)
                expectGreaterThan (rates::delayDivisionBeats (i), rates::delayDivisionBeats (i - 1));
            expectEquals (rates::divisionIndex (0.0f, 16), 0);
            expectEquals (rates::divisionIndex (1.0f, 16), 15);
        }

        beginTest ("LFO delay fades in linearly over the delay time");
        {
            DelayRamp r; r.setSampleRate (sr); r.noteOn();
            float v = 0.0f;
            for (int i = 0; i < (int) (sr * 0.5); ++i) v = r.process (1.0f);
            expectWithinAbsoluteError (v, 0.5f, 0.001f);
            for (int i = 0; i < (int) (sr * 0.6); ++i) v = r.process (1.0f);
            expectEquals (v, 1.0f);
        }
    }
};

static LfoTests lfoTests;
