#include "TestHelpers.h"
#include "PluginProcessor.h"

using namespace nsynth;

class ParamTests : public juce::UnitTest
{
public:
    ParamTests() : juce::UnitTest ("Params", "nSynth") {}

    void runTest() override
    {
        beginTest ("registry sizes are the documented ones");
        expectEquals ((int) getLayerParamSpecs().size(), 81);
        expectEquals ((int) getPerformanceParamSpecs().size(), 16);

        beginTest ("suffixes unique, every spec exists for both layers + performance in the APVTS");
        {
            NSynthAudioProcessor proc;
            auto& s = proc.getState();
            juce::StringArray seen;
            for (auto& spec : getLayerParamSpecs())
            {
                expect (! seen.contains (spec.suffix), juce::String ("duplicate suffix ") + spec.suffix);
                seen.add (spec.suffix);
                expect (s.getParameter (layerParamId (Layer::upper, spec.suffix)) != nullptr, spec.suffix);
                expect (s.getParameter (layerParamId (Layer::lower, spec.suffix)) != nullptr, spec.suffix);
            }
            for (auto& spec : getPerformanceParamSpecs())
                expect (s.getParameter (perfParamId (spec.suffix)) != nullptr, spec.suffix);
            expectEquals (proc.getParameters().size(), 2 * 81 + 16);
        }

        beginTest ("choice lists match the DSP enums");
        auto choices = [] (const char* suffix) { return findLayerSpec (suffix)->choices.size(); };
        expectEquals (choices (pid::dds1Wave), toInt (Dds1Wave::alt) + 1);
        expectEquals (choices (pid::dds1Range), toInt (Dds1Range::ft2) + 1);
        expectEquals (choices (pid::dds2Wave), toInt (Dds2Wave::pulse) + 1);
        expectEquals (choices (pid::dds2Range), toInt (Dds2Range::ft2) + 1);
        expectEquals (choices (pid::dds2Mode), toInt (Dds2Mode::syncOrSubSine) + 1);
        expectEquals (choices (pid::superMode), toInt (SuperMode::on) + 1);
        expectEquals (choices (pid::pwmSource), toInt (PwmSource::env1) + 1);
        expectEquals (choices (pid::vcfDrive), toInt (Drive::two) + 1);
        expectEquals (choices (pid::vcaEnvMode), toInt (VcaEnvMode::fixed2) + 1);
        expectEquals (choices (pid::env1Mode), toInt (Env1Mode::loop) + 1);
        expectEquals (choices (pid::lfo1Wave), toInt (Lfo1Wave::hfTrack) + 1);
        expectEquals (choices (pid::lfo2Wave), toInt (Lfo2Wave::noise) + 1);
        expectEquals (choices (pid::lfo2Trigger), toInt (Lfo2Trigger::on) + 1);
        expectEquals (choices (pid::voiceMode), toInt (VoiceMode::poly2) + 1);
        expectEquals (choices (pid::unisonSize), toInt (UnisonSize::fifthOctave) + 1);
        expectEquals (choices (pid::arpMode), toInt (ArpMode::seq) + 1);
        expectEquals (choices (pid::clockDiv), toInt (ClockDiv::eighthTriplet) + 1);
        expectEquals (choices (pid::chorus), toInt (ChorusMode::oneTwo) + 1);

        beginTest ("defaults reproduce the init patch (single sawtooth oscillator)");
        {
            NSynthAudioProcessor proc;
            juce::AudioProcessorValueTreeState& s = proc.getState();
            LayerParamReader reader (s, Layer::upper);
            LayerSnapshot snap;
            reader.read (snap);
            expect (snap.dds1Wave == Dds1Wave::saw);
            expect (snap.dds1Range == Dds1Range::ft8);
            expectEquals (snap.mix, 0.0f);                 // DDS1 only
            expect (snap.superMode == SuperMode::off);
            expectWithinAbsoluteError (snap.lpfHz, 25000.0f, 1.0f); // filter open
            expectEquals (snap.res, 0.0f);
            expectEquals (snap.vcaEnv, 1.0f);
            expect (snap.vcaEnvMode == VcaEnvMode::env2);
            expectEquals (snap.env2Sustain, 1.0f);
            expect (snap.voiceMode == VoiceMode::poly1);   // default voice mode
            expect (snap.binaural);                        // binaural is the default
            expect (snap.clockDiv == ClockDiv::sixteenth); // CLK DIV reset value
        }

        beginTest ("hardware CC numbers are unique per layer and valid");
        {
            juce::Array<int> ccs;
            for (auto& spec : getLayerParamSpecs())
                if (spec.midiCC >= 0)
                {
                    expect (spec.midiCC < 128);
                    expect (! ccs.contains (spec.midiCC), "duplicate CC " + juce::String (spec.midiCC));
                    ccs.add (spec.midiCC);
                }
            expectEquals (findLayerSpec (pid::lpf)->midiCC, 74);
            expectEquals (findLayerSpec (pid::res)->midiCC, 71);
            expectEquals (findLayerSpec (pid::dds1Wave)->midiCC, 29);
            expectEquals (findLayerSpec (pid::voiceMode)->midiCC, 78);
            expectEquals (findLayerSpec (pid::chorus)->midiCC, 93);
        }

        beginTest ("ranges: exponential and skewed mappings round-trip and hit their end points");
        for (auto& spec : getLayerParamSpecs())
        {
            if (spec.kind != Kind::floatParam)
                continue;
            auto r = makeRange (spec);
            expectWithinAbsoluteError (r.convertFrom0to1 (0.0f), spec.minValue, 1.0e-4f * juce::jmax (1.0f, std::abs (spec.minValue)));
            expectWithinAbsoluteError (r.convertFrom0to1 (1.0f), spec.maxValue, 1.0e-3f * juce::jmax (1.0f, std::abs (spec.maxValue)));
            for (float x : { 0.1f, 0.37f, 0.5f, 0.82f })
                expectWithinAbsoluteError (r.convertTo0to1 (r.convertFrom0to1 (x)), x, 1.0e-4f);
            expect (spec.defaultValue >= spec.minValue && spec.defaultValue <= spec.maxValue, spec.suffix);
        }
        {
            auto r = makeRange (*findLayerSpec (pid::lpf));
            // exponential: the fader midpoint is the geometric mean
            expectWithinAbsoluteError (r.convertFrom0to1 (0.5f), std::sqrt (15.0f * 25000.0f), 1.0f);
        }

        beginTest ("value text: units are formatted and parsed back");
        {
            auto& lpf = *findLayerSpec (pid::lpf);
            expectEquals (formatValue (lpf, 440.0f), juce::String ("440 Hz"));
            expectEquals (formatValue (lpf, 2500.0f), juce::String ("2.50 kHz"));
            expectWithinAbsoluteError (parseValue (lpf, "2.5 kHz"), 2500.0f, 0.01f);
            auto& att = *findLayerSpec (pid::env1Attack);
            expectEquals (formatValue (att, 0.25f), juce::String ("250 ms"));
            expectWithinAbsoluteError (parseValue (att, "250 ms"), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (parseValue (att, "1.5"), 1.5f, 1.0e-6f);
            auto& tune = *findLayerSpec (pid::dds2Tune);
            expectEquals (formatValue (tune, 7.0f), juce::String ("+7.00 st"));
            expectWithinAbsoluteError (parseValue (tune, "99"), 7.0f, 1.0e-6f); // clamped to range
            auto& mix = *findLayerSpec (pid::mix);
            expectEquals (formatValue (mix, 0.5f), juce::String ("50 %"));
            expectWithinAbsoluteError (parseValue (mix, "25 %"), 0.25f, 1.0e-6f);

            NSynthAudioProcessor proc;
            auto* split = proc.getState().getParameter (perfParamId (pid::splitPoint));
            expectEquals (split->getCurrentValueAsText(), juce::String ("C4"));
        }
    }
};

static ParamTests paramTests;
