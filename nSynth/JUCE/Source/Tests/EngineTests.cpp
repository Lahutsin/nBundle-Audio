#include "TestHelpers.h"
#include "PluginProcessor.h"

using namespace nsynth;
using namespace nsynth::test;

class EngineTests : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "nSynth") {}

    struct Rendered { std::vector<float> l, r; };

    struct Rig
    {
        explicit Rig (double sampleRate = 48000.0, int blockSize = 256) : sr (sampleRate), block (blockSize)
        {
            proc.setRateAndBufferSizeDetails (sr, block);
            proc.prepareToPlay (sr, block);
        }

        void set (const juce::String& id, float v) { setParam (proc.getState(), id, v); }
        void setLayer (Layer l, const char* suffix, float v) { set (layerParamId (l, suffix), v); }
        void setBoth (const char* suffix, float v) { setLayer (Layer::upper, suffix, v); setLayer (Layer::lower, suffix, v); }
        void setPerf (const char* suffix, float v) { set (perfParamId (suffix), v); }

        // Renders `numSamples`, injecting MIDI at absolute sample positions.
        Rendered render (int numSamples, const std::vector<std::pair<int, juce::MidiMessage>>& events = {})
        {
            Rendered out;
            out.l.reserve ((size_t) numSamples);
            out.r.reserve ((size_t) numSamples);
            juce::AudioBuffer<float> buffer (2, block);
            for (int pos = 0; pos < numSamples; pos += block)
            {
                const int n = juce::jmin (block, numSamples - pos);
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
                juce::MidiBuffer midi;
                for (auto& [t, m] : events)
                    if (t >= pos && t < pos + n)
                        midi.addEvent (m, t - pos);
                proc.getEngine().process (view, nullptr, nullptr, midi, 120.0);
                for (int i = 0; i < n; ++i)
                {
                    out.l.push_back (view.getSample (0, i));
                    out.r.push_back (view.getSample (1, i));
                }
            }
            return out;
        }

        int voices (Layer l) { return proc.getEngine().getActiveVoices (l); }
        const SynthLayer& layer (Layer l) { return proc.getEngine().layer (l); }

        double sr;
        int block;
        NSynthAudioProcessor proc;
    };

    static juce::MidiMessage on (int note, float vel = 1.0f) { return juce::MidiMessage::noteOn (1, note, vel); }
    static juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }

    static std::vector<std::pair<int, juce::MidiMessage>> chord (std::initializer_list<int> notes, int at = 0)
    {
        std::vector<std::pair<int, juce::MidiMessage>> e;
        for (int n : notes) e.push_back ({ at, on (n) });
        return e;
    }

    void runTest() override
    {
        beginTest ("parallel voice rendering matches serial output in dual-layer 8x mode");
        {
            Rig serial (48000.0, 256);
            Rig parallel (48000.0, 256);
            parallel.proc.getEngine().prepare (48000.0, 256, 3);

            for (auto* rig : { &serial, &parallel })
            {
                rig->setPerf (pid::quality, (float) toInt (Quality::x8));
                rig->setPerf (pid::kbdMode, (float) toInt (KeyboardMode::dual));
                rig->setBoth (pid::binaural, 0.0f);
                rig->setBoth (pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            }

            std::vector<std::pair<int, juce::MidiMessage>> events;
            for (int note = 48; note < 56; ++note)
            {
                events.push_back ({ 0, on (note) });
                events.push_back ({ 2048, off (note) });
            }

            const auto reference = serial.render (4096, events);
            const auto multithreaded = parallel.render (4096, events);
            float maxError = 0.0f;
            for (size_t i = 0; i < reference.l.size(); ++i)
                maxError = juce::jmax (maxError, std::abs (reference.l[i] - multithreaded.l[i]),
                                       std::abs (reference.r[i] - multithreaded.r[i]));

            expectEquals (parallel.proc.getEngine().getRenderWorkerCount(), 3);
            expect (allFinite (multithreaded.l) && allFinite (multithreaded.r));
            expectLessThan (maxError, 1.0e-5f);
        }

        beginTest ("fixed modulation routes affect per-voice DSP targets");
        {
            Rig dry;
            Rig routed;
            for (auto* rig : { &dry, &routed })
            {
                rig->setLayer (Layer::upper, pid::dds2Wave, (float) toInt (Dds2Wave::square));
                rig->setLayer (Layer::upper, pid::mix, 0.0f);
            }
            routed.proc.getEngine().modMatrix (Layer::upper).setFixedAmount (
                ModSource::velocity, ModTarget::oscillatorMix, 1.0f);
            const auto a = dry.render (4096, { { 0, on (60, 1.0f) } });
            const auto b = routed.render (4096, { { 0, on (60, 1.0f) } });
            float maxDifference = 0.0f;
            for (size_t i = 0; i < a.l.size(); ++i)
                maxDifference = juce::jmax (maxDifference, std::abs (a.l[i] - b.l[i]));
            expectGreaterThan (maxDifference, 1.0e-3f);
        }

        beginTest ("direct modulation maps normalized velocity to a continuous voice parameter");
        {
            Rig dry, routed;
            routed.proc.getEngine().modMatrix (Layer::upper).setDirectAmount (
                ModSource::velocity, DirectModTarget::vcaEnvLevel, -1.0f);
            const auto a = dry.render (4096, { { 0, on (60, 1.0f) } });
            const auto b = routed.render (4096, { { 0, on (60, 1.0f) } });
            expectGreaterThan (rms (a.l), 1.0e-3);
            expectLessThan (rms (b.l), 1.0e-6);
        }

        beginTest ("assigning ribbon to the matrix disables its direct pitch bend");
        {
            Rig bent, matrixRouted;
            for (auto* rig : { &bent, &matrixRouted })
            {
                rig->setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
                rig->setPerf (pid::ribbon, 1.0f);
            }
            expect (matrixRouted.proc.getEngine().modMatrix (Layer::upper).setDirectAmount (
                ModSource::ribbon, DirectModTarget::vcaEnvLevel, 0.01f));
            const auto a = bent.render (12000, { { 0, on (69) } });
            const auto b = matrixRouted.render (12000, { { 0, on (69) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (a.l, 48000.0, 2400), 880.0, 0.3);
            expectWithinAbsoluteError (zeroCrossingFrequency (b.l, 48000.0, 2400), 440.0, 0.3);
        }

        beginTest ("user wavetable builds off-thread and swaps the live bank at a block boundary");
        {
            Rig rig;
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("nsynth_import", ".ws6");
            std::vector<float> cycle (MipWavetable::kTableSize);
            for (int i = 0; i < MipWavetable::kTableSize; ++i)
                cycle[(size_t) i] = (float) std::sin (
                    juce::MathConstants<double>::twoPi * 3.0 * i / MipWavetable::kTableSize);
            juce::String error;
            expect (WaveBank::writeWs6 (file, cycle, &error), error);
            expect (rig.proc.getEngine().requestWaveImport (7, file, &error), error);

            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer midi;
            for (int attempt = 0; attempt < 500
                 && rig.proc.getEngine().getWaveImportState() == SynthEngine::WaveImportState::building; ++attempt)
            {
                rig.proc.getEngine().process (audio, nullptr, nullptr, midi, 120.0);
                juce::Thread::sleep (1);
            }
            expect (rig.proc.getEngine().getWaveImportState() == SynthEngine::WaveImportState::ready,
                    rig.proc.getEngine().getWaveImportError());
            expectEquals (rig.proc.getEngine().getWaveBank().altCycle (7).size(), (size_t) MipWavetable::kTableSize);
            expectWithinAbsoluteError (rig.proc.getEngine().getWaveBank().alt (7).read (0, 0.25), -1.0f, 0.01f);
            file.deleteFile();
        }

        beginTest ("sequencer emits sample-clocked notes and HOLD transposes the sequence from C4");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::arpOn, 1.0f);
            rig.setLayer (Layer::upper, pid::arpMode, (float) toInt (ArpMode::seq));
            rig.setLayer (Layer::upper, pid::hold, 1.0f);
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            SequenceStep step;
            step.notes[0] = 0;
            step.noteCount = 1;
            step.rest = false;
            expect (rig.proc.getEngine().sequenceBank (Layer::upper).setStep (0, 0, step));
            const auto audio = rig.render (6000, { { 0, on (72) }, { 100, off (72) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (audio.l, 48000.0), 523.251, 1.0);
            expect (allFinite (audio.l) && allFinite (audio.r));
        }

        beginTest ("arpeggiator steps through held notes in Up/Down/Up&Down modes and HOLD latches");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::arpOn, 1.0f);
            rig.setLayer (Layer::upper, pid::arpMode, (float) toInt (ArpMode::up));
            rig.setLayer (Layer::upper, pid::hold, 1.0f);
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            // 120 BPM, 1/16 = 0.25 beats = 0.125 s = 6000 samples per step at 48 kHz
            const auto audio = rig.render (12000, { { 0, on (60) }, { 0, on (69) }, { 100, off (60) }, { 100, off (69) } });
            std::vector<float> step1 (audio.l.begin() + 600, audio.l.begin() + 4200);
            std::vector<float> step2 (audio.l.begin() + 6600, audio.l.begin() + 10200);
            expectWithinAbsoluteError (zeroCrossingFrequency (step1, 48000.0), 261.626, 1.5);
            expectWithinAbsoluteError (zeroCrossingFrequency (step2, 48000.0), 440.0, 1.5);
        }

        beginTest ("incoming MIDI notes reach the armed sequence recorder without blocking the audio thread");
        {
            Rig rig;
            auto& bank = rig.proc.getEngine().sequenceBank (Layer::upper);
            bank.setRecording (true, 6, 21);
            rig.render (256, { { 0, on (64) }, { 128, off (64) } });
            SequenceBank::RecordedNote recorded;
            expect (bank.popRecordedNote (recorded));
            expectEquals (recorded.slot, 6);
            expectEquals (recorded.step, 21);
            expectEquals (recorded.interval, 4);
            bank.setRecording (false, 0, 0);
        }

        beginTest ("no notes -> digital silence");
        {
            Rig rig;
            auto out = rig.render (48000);
            expectEquals (peak (out.l), 0.0f);
            expectEquals (peak (out.r), 0.0f);
        }

        beginTest ("end-to-end pitch: DDS1 sine, A4 -> 440 Hz at every oversampling factor; latency reported");
        for (auto q : { Quality::x2, Quality::x4, Quality::x8 })
        {
            Rig rig;
            rig.setPerf (pid::quality, (float) toInt (q));
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            auto out = rig.render (48000, { { 0, on (69) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (out.l, 48000.0, 4800), 440.0, 0.1);
            expect (allFinite (out.l));
            expectGreaterThan (rig.proc.getEngine().getLatencySamples(), 0.0f);
        }

        beginTest ("RANGE 4' doubles, transpose/perf detune shift, LOWER detune only affects the lower layer");
        {
            Rig rig;
            rig.setBoth (pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            rig.setLayer (Layer::upper, pid::dds1Range, (float) toInt (Dds1Range::ft4));
            auto a = rig.render (48000, { { 0, on (57) } }); // A3 at 4' = 440
            expectWithinAbsoluteError (zeroCrossingFrequency (a.l, 48000.0, 4800), 440.0, 0.1);

            Rig rig2;
            rig2.setBoth (pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            rig2.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::split));
            rig2.setPerf (pid::lowerDetune, 7.0f);
            rig2.setPerf (pid::transpose, 12.0f);
            auto lowerOnly = rig2.render (48000, { { 0, on (45) } }); // A2 -> +12 +7 st
            expectWithinAbsoluteError (zeroCrossingFrequency (lowerOnly.l, 48000.0, 4800), 110.0 * std::pow (2.0, 19.0 / 12.0), 0.2);
        }

        beginTest ("10-note chord: finite with headroom");
        {
            Rig rig;
            auto out = rig.render (24000, chord ({ 48, 52, 55, 59, 62, 64, 67, 71, 74, 77 }));
            expect (allFinite (out.l) && allFinite (out.r));
            expectLessThan (peak (out.l), 2.0f);
            expectGreaterThan (rms (out.l), 0.01);
        }

        beginTest ("SINGLE + binaural: 10 stereo super voices = 20 hardware voices");
        {
            Rig rig;
            rig.render (512, chord ({ 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50 }));
            expectEquals (rig.voices (Layer::upper), 20);
            expectEquals (rig.voices (Layer::lower), 0);
            expectEquals (rig.layer (Layer::upper).getUnitSize(), 2);
        }

        beginTest ("SINGLE without binaural: 20 mono voices");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            std::vector<std::pair<int, juce::MidiMessage>> e;
            for (int n = 40; n < 62; ++n) e.push_back ({ 0, on (n) });
            rig.render (512, e);
            expectEquals (rig.voices (Layer::upper), 20);
            expectEquals (rig.layer (Layer::upper).getUnitSize(), 1);
        }

        beginTest ("DUAL: both layers sound, 10 voices each (5 binaural notes)");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::dual));
            rig.render (512, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 2);
            expectEquals (rig.voices (Layer::lower), 2);
            rig.render (512, chord ({ 61, 62, 63, 64, 65, 66 }));
            expectEquals (rig.voices (Layer::upper), 10);
            expectEquals (rig.voices (Layer::lower), 10);
        }

        beginTest ("SPLIT: below the split point -> LOWER, from the split point -> UPPER");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::split));
            rig.setPerf (pid::splitPoint, 60.0f);
            rig.render (256, chord ({ 59 }));
            expectEquals (rig.voices (Layer::lower), 2);
            expectEquals (rig.voices (Layer::upper), 0);
            rig.render (256, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 2);
        }

        beginTest ("POLY 1 lets releases overlap, POLY 2 curtails them");
        {
            for (auto mode : { VoiceMode::poly1, VoiceMode::poly2 })
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (mode));
                rig.setLayer (Layer::upper, pid::env2Release, 2.0f);
                rig.render (4800, { { 0, on (60) }, { 2400, off (60) }, { 2400 + 256, on (64) } });
                rig.render (4800); // 100 ms later
                expectEquals (rig.voices (Layer::upper), mode == VoiceMode::poly1 ? 4 : 2);
            }
        }

        beginTest ("SOLO retriggers the envelopes, LEGATO does not");
        {
            for (auto mode : { VoiceMode::solo, VoiceMode::legato })
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (mode));
                rig.setLayer (Layer::upper, pid::env2Attack, 0.5f);
                rig.setLayer (Layer::upper, pid::env2Decay, 0.3f);
                rig.setLayer (Layer::upper, pid::env2Sustain, 0.2f);
                rig.render (48000, { { 0, on (60) } });
                rig.render (512, { { 0, on (64) } });
                auto* v = rig.layer (Layer::upper).getVoices().front();
                expectEquals (v->getNote(), 64);
                const auto stage = v->getAmpEnvelope().getStage();
                if (mode == VoiceMode::solo) expect (stage == Envelope::Stage::attack);
                else                         expect (stage != Envelope::Stage::attack);
                expectEquals (rig.voices (Layer::upper), 2); // still one binaural voice
            }
        }

        beginTest ("mono: releasing the newest key returns to the previously held one");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (VoiceMode::legato));
            rig.render (1024, { { 0, on (60) }, { 256, on (67) }, { 512, off (67) } });
            expectEquals (rig.layer (Layer::upper).getVoices().front()->getNote(), 60);
            expect (rig.layer (Layer::upper).getVoices().front()->isGateOn());
        }

        beginTest ("UNISON in SOLO stacks all voices; U.SIZE octave alternates +12");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (VoiceMode::solo));
            rig.setLayer (Layer::upper, pid::unison, 1.0f);
            rig.setLayer (Layer::upper, pid::unisonSize, (float) toInt (UnisonSize::octave));
            rig.render (512, chord ({ 48 }));
            expectEquals (rig.voices (Layer::upper), 20);
            int octaveUp = 0;
            for (auto* v : rig.layer (Layer::upper).getVoices())
                octaveUp += std::abs (v->getCurrentPitch() - 60.0f) < 0.01f ? 1 : 0;
            expectEquals (octaveUp, 10);

            rig.setLayer (Layer::upper, pid::unisonSize, (float) toInt (UnisonSize::half));
            rig.render (512, { { 0, off (48) } });
            rig.render (48000);
            rig.render (512, chord ({ 50 }));
            expectEquals (rig.voices (Layer::upper), 10);
        }

        beginTest ("UNISON in POLY divides the stacked voices by the number of notes");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::unison, 1.0f);
            rig.setLayer (Layer::upper, pid::env2Release, 0.001f);
            rig.render (256, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 20);
            rig.render (4800, chord ({ 64 }));
            int onFirst = 0, onSecond = 0;
            for (auto* v : rig.layer (Layer::upper).getVoices())
                if (v->isGateOn()) (v->getNote() == 60 ? onFirst : onSecond)++;
            expectEquals (onFirst, 10);
            expectEquals (onSecond, 10);
        }

        beginTest ("binaural: left and right differ (super detune mirrored); mono path with SPREAD 0 is centred");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::superMode, (float) toInt (SuperMode::on));
            rig.setLayer (Layer::upper, pid::pwDetune, 0.6f);
            auto out = rig.render (24000, chord ({ 57 }));
            double diff = 0.0;
            for (size_t i = 0; i < out.l.size(); ++i) diff += std::abs (out.l[i] - out.r[i]);
            expectGreaterThan (diff / (double) out.l.size(), 0.01);

            Rig mono;
            mono.setLayer (Layer::upper, pid::binaural, 0.0f);
            mono.setLayer (Layer::upper, pid::lfo1LrPhase, 0.0f);
            auto m = mono.render (24000, chord ({ 57 }));
            double mdiff = 0.0;
            for (size_t i = 0; i < m.l.size(); ++i) mdiff += std::abs (m.l[i] - m.r[i]);
            expectLessThan (mdiff / (double) m.l.size(), 1.0e-6);
        }

        beginTest ("portamento time is proportional to the interval (10 s per octave at max)");
        {
            auto glideSamples = [&] (int interval)
            {
                // SOLO: the same voice moves from note to note, so it glides from the held key.
                Rig rig;
                rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (VoiceMode::solo));
                rig.setLayer (Layer::upper, pid::portamento, 0.5f);
                rig.render (256, chord ({ 48 }));
                rig.render (256, chord ({ 48 + interval }));
                auto* v = rig.layer (Layer::upper).getVoices().front();
                int n = 256;
                while (std::abs (v->getCurrentPitch() - (float) (48 + interval)) > 0.01f && n < 48000 * 30)
                {
                    rig.render (256);
                    n += 256;
                }
                return n;
            };
            const double t12 = glideSamples (12) / 48000.0, t24 = glideSamples (24) / 48000.0;
            expectWithinAbsoluteError (t12, (double) Voice::portamentoSecondsPerOctave (0.5f), 0.02);
            expectWithinAbsoluteError (t24 / t12, 2.0, 0.05);
        }

        beginTest ("sustain pedal and HOLD keep notes until released");
        {
            Rig rig;
            rig.render (512, { { 0, on (60) }, { 10, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 20, off (60) } });
            rig.render (4800);
            expectEquals (rig.voices (Layer::upper), 2);
            expect (rig.layer (Layer::upper).getVoices().front()->isGateOn());
            rig.render (512, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
            expect (! rig.layer (Layer::upper).getVoices().front()->isGateOn());

            Rig hold;
            hold.setLayer (Layer::upper, pid::hold, 1.0f);
            hold.render (512, { { 0, on (62) }, { 100, off (62) } });
            expect (hold.layer (Layer::upper).getVoices().front()->isGateOn());
            hold.setLayer (Layer::upper, pid::hold, 0.0f);
            hold.render (256);
            expect (! hold.layer (Layer::upper).getVoices().front()->isGateOn());
        }

        beginTest ("dual sustain pedal routes channel 1 to UPPER and channel 2 to LOWER");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::dual));
            rig.setPerf (pid::dualPedal, 1.0f);
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::lower, pid::binaural, 0.0f);
            rig.render (256, {
                { 0, on (60) },
                { 32, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 64, off (60) }
            });

            const auto& upperVoices = rig.layer (Layer::upper).getVoices();
            const auto& lowerVoices = rig.layer (Layer::lower).getVoices();
            bool upperGate = false, lowerGate = false;
            for (auto* voice : upperVoices) upperGate |= voice->isGateOn();
            for (auto* voice : lowerVoices) lowerGate |= voice->isGateOn();
            expect (upperGate);
            expect (! lowerGate);
        }

        beginTest ("all supported sample rates produce finite audio");
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Rig rig (sr, 333);
            rig.setLayer (Layer::upper, pid::res, 0.95f);
            rig.setLayer (Layer::upper, pid::lpf, 800.0f);
            rig.setLayer (Layer::upper, pid::crossMod, 0.7f);
            rig.setLayer (Layer::upper, pid::dds2Mode, (float) toInt (Dds2Mode::syncOrSubSine));
            rig.setLayer (Layer::upper, pid::mix, 0.5f);
            auto out = rig.render ((int) sr / 2, chord ({ 36, 60, 84, 96 }));
            expect (allFinite (out.l) && allFinite (out.r), juce::String (sr));
            expectGreaterThan (rms (out.l), 1.0e-4);
        }
    }
};

static EngineTests engineTests;
