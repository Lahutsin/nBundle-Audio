#include "TestHelpers.h"
#include "PluginProcessor.h"

using namespace nsynth;
using namespace nsynth::test;

class MidiInputTests : public juce::UnitTest
{
public:
    MidiInputTests() : juce::UnitTest ("MidiInput", "nSynth") {}

    struct Rig
    {
        explicit Rig (int blockSize = 256) : block (blockSize)
        {
            processor.setRateAndBufferSizeDetails (48000.0, block);
            processor.prepareToPlay (48000.0, block);
            setParam (processor.getState(), layerParamId (Layer::upper, pid::dds1Wave), (float) toInt (Dds1Wave::sine));
            setParam (processor.getState(), layerParamId (Layer::upper, pid::binaural), 0.0f);
        }

        using Event = std::pair<int, juce::MidiMessage>;

        void process (int samples, const std::vector<Event>& events = {})
        {
            juce::AudioBuffer<float> buffer (2, block);
            for (int position = 0; position < samples; position += block)
            {
                const int count = juce::jmin (block, samples - position);
                juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, count);
                juce::MidiBuffer midi;
                for (const auto& [at, message] : events)
                    if (at >= position && at < position + count)
                        midi.addEvent (message, at - position);
                processor.getEngine().process (slice, nullptr, nullptr, midi, 120.0);
            }
        }

        std::vector<float> render (int samples, const std::vector<Event>& events)
        {
            std::vector<float> left;
            left.reserve ((size_t) samples);
            juce::AudioBuffer<float> buffer (2, block);
            for (int position = 0; position < samples; position += block)
            {
                const int count = juce::jmin (block, samples - position);
                juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, count);
                juce::MidiBuffer midi;
                for (const auto& [at, message] : events)
                    if (at >= position && at < position + count)
                        midi.addEvent (message, at - position);
                processor.getEngine().process (slice, nullptr, nullptr, midi, 120.0);
                for (int i = 0; i < count; ++i)
                    left.push_back (slice.getSample (0, i));
            }
            return left;
        }

        NSynthAudioProcessor processor;
        int block;
    };

    static juce::MidiMessage cc (int channel, int number, int value)
    {
        return juce::MidiMessage::controllerEvent (channel, number, value);
    }

    static juce::MidiMessage noteOn (int channel, int note)
    {
        return juce::MidiMessage::noteOn (channel, note, (juce::uint8) 127);
    }

    void runTest() override
    {
        beginTest ("hardware CC map is opt-in and maps the manual CC to the active layer parameter");
        {
            Rig rig;
            rig.process (256, { { 0, cc (1, 74, 127) } });
            expect (! rig.processor.getEngine().hasPendingMidiParameterChanges());

            setParam (rig.processor.getState(), perfParamId (pid::hardwareCCMap), 1.0f);
            rig.process (256, { { 0, cc (1, 74, 64) } });
            SynthEngine::MidiParameterChange change;
            expect (rig.processor.getEngine().popMidiParameterChange (change));
            expect (change.parameter == rig.processor.getState().getParameter (layerParamId (Layer::upper, pid::lpf)));
            expectWithinAbsoluteError (change.normalizedValue, 64.0f / 127.0f, 1.0e-5f);
            expect (! rig.processor.getEngine().popMidiParameterChange (change));
        }

        beginTest ("channel-per-layer routing and 14-bit NRPN receive target the documented parameter");
        {
            Rig rig;
            setParam (rig.processor.getState(), perfParamId (pid::hardwareCCMap), 1.0f);
            setParam (rig.processor.getState(), perfParamId (pid::midiRouting), 1.0f);
            rig.process (256, { { 0, cc (2, 74, 96) } });
            SynthEngine::MidiParameterChange change;
            expect (rig.processor.getEngine().popMidiParameterChange (change));
            expect (change.parameter == rig.processor.getState().getParameter (layerParamId (Layer::lower, pid::lpf)));
            expect (! rig.processor.getEngine().popMidiParameterChange (change));

            rig.process (256, { { 0, cc (1, 99, 8) }, { 1, cc (1, 98, 74) },
                                { 2, cc (1, 6, 127) }, { 3, cc (1, 38, 127) } });
            expect (rig.processor.getEngine().popMidiParameterChange (change));
            expect (change.parameter == rig.processor.getState().getParameter (layerParamId (Layer::upper, pid::lpf)));
            expect (rig.processor.getEngine().popMidiParameterChange (change));
            expectWithinAbsoluteError (change.normalizedValue, 1.0f, 1.0e-6f);
        }

        beginTest ("RPN pitch sensitivity and channel fine/coarse tuning affect note pitch");
        {
            auto renderTuned = [] (std::vector<Rig::Event> events)
            {
                Rig rig;
                events.push_back ({ 8, noteOn (1, 69) });
                return rig.render (24000, events);
            };
            const auto coarse = renderTuned ({ { 0, cc (1, 101, 0) }, { 1, cc (1, 100, 2) }, { 2, cc (1, 6, 65) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (coarse, 48000.0, 2400), 466.164, 0.4);
            const auto fine = renderTuned ({ { 0, cc (1, 101, 0) }, { 1, cc (1, 100, 1) },
                                             { 2, cc (1, 6, 96) }, { 3, cc (1, 38, 0) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (fine, 48000.0, 2400), 452.893, 0.4);
            const auto bend = renderTuned ({ { 0, cc (1, 101, 0) }, { 1, cc (1, 100, 0) },
                                             { 2, cc (1, 6, 12) }, { 3, juce::MidiMessage::pitchWheel (1, 16383) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (bend, 48000.0, 2400), 880.0, 1.0);
        }

        beginTest ("MPE RPN zone routes per-note pitch, channel pressure and CC74 timbre to modulation sources");
        {
            Rig rig;
            rig.processor.getEngine().modMatrix (Layer::upper).setDirectAmount (
                ModSource::timbre, DirectModTarget::vcaEnvLevel, 0.0f);
            rig.process (512, {
                { 0, cc (1, 101, 0) }, { 1, cc (1, 100, 6) }, { 2, cc (1, 6, 1) },
                { 4, noteOn (2, 60) }, { 5, juce::MidiMessage::channelPressureChange (2, 100) },
                { 6, cc (2, 74, 127) }, { 7, juce::MidiMessage::pitchWheel (2, 12000) },
                { 8, noteOn (1, 60) }
            });

            const Voice* member = nullptr;
            const Voice* nonMember = nullptr;
            for (auto* voice : rig.processor.getEngine().layer (Layer::upper).getVoices())
                if (voice->isActive() && voice->getMidiChannel() == 2)
                {
                    member = voice;
                }
                else if (voice->isActive() && voice->getMidiChannel() == 1)
                    nonMember = voice;
            expect (member != nullptr);
            expect (nonMember != nullptr);
            if (member != nullptr)
            {
                const auto& sources = member->getLastModSources();
                expectWithinAbsoluteError (sources[(size_t) ModSource::polyAftertouch], 100.0f / 127.0f, 1.0e-5f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::perNotePitch],
                                           0.5f + 0.5f * (12000.0f - 8192.0f) / 8192.0f, 1.0e-5f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::timbre], 1.0f, 1.0e-6f);
            }
            if (nonMember != nullptr)
            {
                const auto& sources = nonMember->getLastModSources();
                expectWithinAbsoluteError (sources[(size_t) ModSource::polyAftertouch], 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::perNotePitch], 0.5f, 1.0e-6f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::timbre], 0.0f, 1.0e-6f);
            }
        }

        beginTest ("CC121 resets controller expression without resetting pitch bend");
        {
            Rig rig;
            rig.process (512, {
                { 0, cc (1, 101, 0) }, { 1, cc (1, 100, 6) }, { 2, cc (1, 6, 1) },
                { 4, juce::MidiMessage::pitchWheel (2, 12000) },
                { 5, juce::MidiMessage::channelPressureChange (2, 100) },
                { 6, cc (2, 74, 127) }, { 7, noteOn (2, 60) }, { 8, cc (2, 121, 0) }
            });

            const Voice* member = nullptr;
            for (auto* voice : rig.processor.getEngine().layer (Layer::upper).getVoices())
                if (voice->isActive() && voice->getMidiChannel() == 2)
                    member = voice;
            expect (member != nullptr);
            if (member != nullptr)
            {
                const auto& sources = member->getLastModSources();
                expectWithinAbsoluteError (sources[(size_t) ModSource::polyAftertouch], 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::timbre], 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (sources[(size_t) ModSource::perNotePitch],
                                           0.5f + 0.5f * (12000.0f - 8192.0f) / 8192.0f, 1.0e-5f);
            }
        }

        beginTest ("MIDI learn assignment is applied, cleared and persisted in plugin state");
        {
            Rig rig;
            const auto target = layerParamId (Layer::upper, pid::res);
            expect (rig.processor.getEngine().requestMidiLearn (target));
            rig.process (256, { { 0, cc (1, 21, 127) } });
            expectEquals (rig.processor.getEngine().getMidiLearnCC (target), 21);
            SynthEngine::MidiParameterChange change;
            expect (rig.processor.getEngine().popMidiParameterChange (change));
            expect (change.parameter == rig.processor.getState().getParameter (target));

            juce::MemoryBlock state;
            rig.processor.getStateInformation (state);
            NSynthAudioProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (restored.getEngine().getMidiLearnCC (target), 21);
            restored.getEngine().clearMidiLearn (target);
            expectEquals (restored.getEngine().getMidiLearnCC (target), -1);
        }
    }
};

static MidiInputTests midiInputTests;
