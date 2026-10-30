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
        beginTest ("parallel voice rendering matches serial output in dual-layer mode");
        {
            Rig serial (48000.0, 256);
            Rig parallel (48000.0, 256);
            parallel.proc.getEngine().prepare (48000.0, 256, 3);

            for (auto* rig : { &serial, &parallel })
            {
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
                rig->setLayer (Layer::upper, pid::binaural, (float) toInt (BinauralMode::x1)); // one voice per ear: exact pitch
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
            expect (rig.proc.getEngine().requestWaveImport (Layer::upper, 7, file, &error), error);

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
            expectEquals (rig.proc.getEngine().getWaveBank (Layer::upper)->altCycle (7).size(), (size_t) MipWavetable::kTableSize);
            expectWithinAbsoluteError (rig.proc.getEngine().getWaveBank (Layer::upper)->alt (7).read (0, 0.25), -1.0f, 0.01f);
            file.deleteFile();
        }

        beginTest ("POLY + UNISON: adding notes re-distributes the stack without clicks");
        {
            // A click shows up as a spike in the second difference of an otherwise smooth (sine) signal.
            auto roughness = [] (const std::vector<float>& x, size_t from, size_t to)
            {
                float worst = 0.0f;
                for (size_t i = juce::jmax ((size_t) 2, from); i < to && i < x.size(); ++i)
                    worst = juce::jmax (worst, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
                return worst;
            };
            for (auto binaural : { BinauralMode::off, BinauralMode::x2 })
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
                rig.setLayer (Layer::upper, pid::binaural, (float) toInt (binaural));
                rig.setLayer (Layer::upper, pid::unison, 1.0f);
                // A 10 ms attack: the onsets themselves are smooth, so what remains is the voice handover.
                rig.setLayer (Layer::upper, pid::env2Attack, 0.01f);
                rig.setPerf (pid::masterVolume, -12.0f);   // stay clear of the output limiter
                const auto out = rig.render (43200, { { 0, on (60) }, { 14400, on (64) }, { 28800, on (67) } });
                // Each later onset against the same chord once it has settled and against the first
                // note's onset (no voice is taken over there; its attack corner sets the floor):
                // a click stands out, the notes' own curvature and attack do not.
                const float firstOnset = roughness (out.l, 0, 2400);
                for (int onset : { 14400, 28800 })
                {
                    const float atOnset = roughness (out.l, (size_t) onset, (size_t) onset + 2400);
                    const float settled = roughness (out.l, (size_t) onset + 9600, (size_t) onset + 14400);
                    expect (atOnset < 2.0f * juce::jmax (settled, firstOnset),
                            "note at " + juce::String (onset) + " clicks: " + juce::String (atOnset, 5) + " vs "
                                + juce::String (juce::jmax (settled, firstOnset), 5));
                }
            }
        }

        beginTest ("no notes -> digital silence");
        {
            Rig rig;
            auto out = rig.render (48000);
            expectEquals (peak (out.l), 0.0f);
            expectEquals (peak (out.r), 0.0f);
        }

        beginTest ("end-to-end pitch: DDS1 sine, A4 -> 440 Hz with fixed 2x oversampling; latency reported");
        {
            Rig rig;
            expectEquals (rig.proc.getEngine().getOversamplingFactor(), 2);
            rig.setLayer (Layer::upper, pid::binaural, (float) toInt (BinauralMode::x1));
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            auto out = rig.render (48000, { { 0, on (69) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (out.l, 48000.0, 4800), 440.0, 0.1);
            expect (allFinite (out.l));
            expectGreaterThan (rig.proc.getEngine().getLatencySamples(), 0.0f);
        }

        beginTest ("RANGE 4' doubles, transpose/perf detune shift, LOWER detune only affects the lower layer");
        {
            Rig rig;
            rig.setBoth (pid::binaural, (float) toInt (BinauralMode::x1));
            rig.setBoth (pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            rig.setLayer (Layer::upper, pid::dds1Range, (float) toInt (Dds1Range::ft4));
            auto a = rig.render (48000, { { 0, on (57) } }); // A3 at 4' = 440
            expectWithinAbsoluteError (zeroCrossingFrequency (a.l, 48000.0, 4800), 440.0, 0.1);

            Rig rig2;
            rig2.setBoth (pid::binaural, (float) toInt (BinauralMode::x1));
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

        beginTest ("SINGLE + binaural x2: 32 notes x 4 voices = 128 hardware voices; x1: 64 stereo notes");
        {
            Rig rig;
            expectEquals (SynthEngine::kNumVoices, 128);
            std::vector<std::pair<int, juce::MidiMessage>> e;
            for (int n = 40; n < 73; ++n) e.push_back ({ 0, on (n) });
            rig.render (512, e);
            expectEquals (rig.voices (Layer::upper), 128);
            expectEquals (rig.voices (Layer::lower), 0);
            expectEquals (rig.layer (Layer::upper).getUnitSize(), 4);

            Rig x1;
            x1.setLayer (Layer::upper, pid::binaural, (float) toInt (BinauralMode::x1));
            std::vector<std::pair<int, juce::MidiMessage>> e1;
            for (int n = 30; n < 95; ++n) e1.push_back ({ 0, on (n) });
            x1.render (512, e1);
            expectEquals (x1.voices (Layer::upper), 128);
            expectEquals (x1.layer (Layer::upper).getUnitSize(), 2);
        }

        beginTest ("binaural x2: detuned pair per ear, same loudness as x1, DETUNE 0 collapses the pair");
        {
            auto render = [&] (BinauralMode mode, float detune)
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::binaural, (float) toInt (mode));
                rig.setLayer (Layer::upper, pid::binauralDetune, detune);
                rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
                return rig.render (48000, chord ({ 69 }));
            };
            const auto x1 = render (BinauralMode::x1, 7.0f);
            const auto x2 = render (BinauralMode::x2, 7.0f);
            const auto x2Flat = render (BinauralMode::x2, 0.0f);
            expect (allFinite (x2.l) && allFinite (x2.r));
            // The pair beats at 2 x 7 cents (~3.6 Hz at A4): the envelope of x2 moves, x1 does not.
            auto envelopeSpread = [] (const std::vector<float>& x)
            {
                float lo = 1.0e9f, hi = 0.0f;
                for (size_t start = 4800; start + 480 <= x.size(); start += 480)
                {
                    const float p = peak (std::vector<float> (x.begin() + (long) start, x.begin() + (long) start + 480));
                    lo = juce::jmin (lo, p);
                    hi = juce::jmax (hi, p);
                }
                return hi / juce::jmax (lo, 1.0e-9f);
            };
            expectLessThan (envelopeSpread (x1.l), 1.05f);
            expectGreaterThan (envelopeSpread (x2.l), 1.5f);
            expectWithinAbsoluteError (toDb (rms (x2.l, 4800)), toDb (rms (x1.l, 4800)), 2.0);
            expectLessThan (envelopeSpread (x2Flat.l), 1.05f);
        }

        beginTest ("SINGLE without binaural: 128 mono voices");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            std::vector<std::pair<int, juce::MidiMessage>> e;
            for (int n = 0; n < 128; ++n) e.push_back ({ 0, on (n) });
            rig.render (512, e);
            expectEquals (rig.voices (Layer::upper), 128);
            expectEquals (rig.layer (Layer::upper).getUnitSize(), 1);
        }

        beginTest ("DUAL: both layers sound, 64 voices each (16 binaural x2 notes)");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::dual));
            rig.render (512, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 4);
            expectEquals (rig.voices (Layer::lower), 4);
            rig.render (512, chord ({ 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76 }));
            expectEquals (rig.voices (Layer::upper), 64);
            expectEquals (rig.voices (Layer::lower), 64);
        }

        beginTest ("SPLIT: below the split point -> LOWER, from the split point -> UPPER");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::split));
            rig.setPerf (pid::splitPoint, 60.0f);
            rig.render (256, chord ({ 59 }));
            expectEquals (rig.voices (Layer::lower), 4);
            expectEquals (rig.voices (Layer::upper), 0);
            rig.render (256, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 4);
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
                expectEquals (rig.voices (Layer::upper), mode == VoiceMode::poly1 ? 8 : 4);
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
                expectEquals (rig.voices (Layer::upper), 4); // still one binaural x2 voice
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
            expectEquals (rig.voices (Layer::upper), 128);
            int octaveUp = 0;
            for (auto* v : rig.layer (Layer::upper).getVoices())
                octaveUp += std::abs (v->getCurrentPitch() - 60.0f) < 0.01f ? 1 : 0;
            expectEquals (octaveUp, 64);

            rig.setLayer (Layer::upper, pid::unisonSize, (float) toInt (UnisonSize::half));
            rig.render (512, { { 0, off (48) } });
            rig.render (48000);
            rig.render (512, chord ({ 50 }));
            expectEquals (rig.voices (Layer::upper), 64);
        }

        beginTest ("retriggering a sounding voice keeps the oscillator phase (no click)");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (VoiceMode::solo));
            rig.setLayer (Layer::upper, pid::dds2Wave, (float) toInt (Dds2Wave::sine));
            rig.setLayer (Layer::upper, pid::mix, 1.0f);
            rig.setLayer (Layer::upper, pid::drift, 0.0f);
            const int retriggerAt = 4801;
            const auto out = rig.render (9600, { { 0, on (57) }, { retriggerAt, on (57) } });
            float steady = 0.0f, atRetrigger = 0.0f;
            for (int i = 2400; i < 4700; ++i)
                steady = juce::jmax (steady, std::abs (out.l[(size_t) i] - out.l[(size_t) i - 1]));
            for (int i = retriggerAt - 4; i < retriggerAt + 64; ++i)
                atRetrigger = juce::jmax (atRetrigger, std::abs (out.l[(size_t) i] - out.l[(size_t) i - 1]));
            expectGreaterThan (steady, 0.0f);
            expectLessThan (atRetrigger, steady * 1.5f);
        }

        beginTest ("UNISON stacks are detuned and level-matched to a single voice");
        {
            auto render = [&] (bool unison)
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::binaural, 0.0f);
                rig.setLayer (Layer::upper, pid::voiceMode, (float) toInt (VoiceMode::solo));
                rig.setLayer (Layer::upper, pid::unison, unison ? 1.0f : 0.0f);
                rig.setLayer (Layer::upper, pid::unisonSize, (float) toInt (UnisonSize::half));
                return rig.render (48000, chord ({ 57 }));
            };
            const auto single = render (false), stacked = render (true);
            expect (allFinite (stacked.l));
            // Uncorrelated, detuned voices add in power: a 1/sqrt(n) stack stays near the single-voice level.
            expectWithinAbsoluteError (toDb (rms (stacked.l, 9600)), toDb (rms (single.l, 9600)), 3.0);
            expectLessThan (peak (stacked.l), 1.0f);
        }

        beginTest ("UNISON in POLY divides the stacked voices by the number of notes");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::unison, 1.0f);
            rig.setLayer (Layer::upper, pid::env2Release, 0.001f);
            rig.render (256, chord ({ 60 }));
            expectEquals (rig.voices (Layer::upper), 128);
            rig.render (4800, chord ({ 64 }));
            int onFirst = 0, onSecond = 0;
            for (auto* v : rig.layer (Layer::upper).getVoices())
                if (v->isGateOn()) (v->getNote() == 60 ? onFirst : onSecond)++;
            expectEquals (onFirst, 64);
            expectEquals (onSecond, 64);
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
            expectEquals (rig.voices (Layer::upper), 4);
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

        beginTest ("velocity: DYNAMIC follows the played velocity and VEL CURVE, STATIC ignores it");
        {
            auto level = [&] (VelocityMode mode, float velocity, float curve, int fixedVelocity)
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::binaural, 0.0f);
                rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
                rig.setLayer (Layer::upper, pid::velSens, 1.0f);
                rig.setLayer (Layer::upper, pid::velMode, (float) toInt (mode));
                rig.setLayer (Layer::upper, pid::velCurve, curve);
                rig.setLayer (Layer::upper, pid::velFixed, (float) fixedVelocity);
                return rms (rig.render (9600, { { 0, on (69, velocity) } }).l, 2400);
            };
            const double loud = level (VelocityMode::dynamic, 1.0f, 0.0f, 100);
            const double soft = level (VelocityMode::dynamic, 0.3f, 0.0f, 100);
            expectLessThan (soft, 0.5 * loud);
            expectLessThan (level (VelocityMode::dynamic, 0.3f, 1.0f, 100), 0.6 * soft);    // hard curve
            expectGreaterThan (level (VelocityMode::dynamic, 0.3f, -1.0f, 100), 1.5 * soft); // soft curve
            expectWithinAbsoluteError (level (VelocityMode::fixed, 0.3f, 0.0f, 127), loud, 1.0e-6 + 1.0e-3 * loud);
            expectWithinAbsoluteError (level (VelocityMode::fixed, 1.0f, 0.0f, 40),
                                       level (VelocityMode::fixed, 0.1f, 0.0f, 40), 1.0e-9);

            Rig rig;
            rig.setLayer (Layer::upper, pid::velMode, (float) toInt (VelocityMode::fixed));
            rig.setLayer (Layer::upper, pid::velFixed, 64.0f);
            rig.render (256);
            expectWithinAbsoluteError (rig.layer (Layer::upper).shapeVelocity (0.9f), 64.0f / 127.0f, 1.0e-6f);
        }

        beginTest ("delay SYNC: a 1/8 echo at 120 BPM arrives 250 ms later; MS mode uses the time in milliseconds");
        {
            auto firstSoundAfter = [&] (bool sync)
            {
                Rig rig;
                rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
                rig.setLayer (Layer::upper, pid::env2Release, 0.001f);
                rig.setLayer (Layer::upper, pid::delayMix, 1.0f);      // wet only
                rig.setLayer (Layer::upper, pid::delayFeedback, 0.0f);
                rig.setLayer (Layer::upper, pid::delaySync, sync ? 1.0f : 0.0f);
                rig.setLayer (Layer::upper, pid::delayDivision, (float) toInt (DelayDivision::d1_8));
                rig.setLayer (Layer::upper, pid::delayMs, 100.0f);
                const auto out = rig.render (24000, { { 0, on (69) }, { 480, off (69) } });
                for (size_t i = 0; i < out.l.size(); ++i)
                    if (std::abs (out.l[i]) > 1.0e-3f)
                        return (int) i;
                return -1;
            };
            expectWithinAbsoluteError (firstSoundAfter (true), 12000, 40);
            expectWithinAbsoluteError (firstSoundAfter (false), 4800, 40);
        }

        beginTest ("reverb and chorus are off by default and colour the sound once enabled");
        {
            Rig dry, wet;
            wet.setLayer (Layer::upper, pid::reverbMix, 0.4f);
            wet.setLayer (Layer::upper, pid::chorus, (float) toInt (ChorusMode::oneTwo));
            const auto a = dry.render (48000, { { 0, on (60) }, { 4800, off (60) } });
            const auto b = wet.render (48000, { { 0, on (60) }, { 4800, off (60) } });
            const std::vector<float> dryTail (a.l.begin() + 24000, a.l.end()), wetTail (b.l.begin() + 24000, b.l.end());
            expectLessThan (rms (dryTail), 1.0e-6);
            expectGreaterThan (rms (wetTail), 1.0e-4);
            expect (allFinite (b.l) && allFinite (b.r));
        }

        beginTest ("open filter is bright: a saw's 25th harmonic (11 kHz) is not rolled off");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::saw));
            const auto out = rig.render (24000, { { 0, on (69) } });
            const double fundamental = toneAmplitude (out.l, 440.0, 48000.0, 4800);
            const double h25 = toneAmplitude (out.l, 25.0 * 440.0, 48000.0, 4800);
            const double relativeDb = toDb (h25 / fundamental) - toDb (1.0 / 25.0); // 0 dB = ideal saw
            expectGreaterThan (relativeDb, -1.0);
            expectLessThan (relativeDb, 3.5);
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

// Random patches (every layer parameter random) must never produce NaN/Inf, and the layer must
// recover: a second note after the random patch is replaced by a plain one is audible again.
class RobustnessTests : public juce::UnitTest
{
public:
    RobustnessTests() : juce::UnitTest ("Robustness", "nSynth") {}

    void runTest() override
    {
        beginTest ("random patches stay finite and the signal path recovers");
        juce::Random master (20261029);
        for (int trial = 0; trial < 48; ++trial)
        {
            NSynthAudioProcessor p;
            juce::Random rng (master.nextInt64());
            for (auto& spec : getLayerParamSpecs())
                p.getState().getParameter (layerParamId (Layer::upper, spec.suffix))->setValueNotifyingHost (rng.nextFloat());
            // Keep the patch audible enough to be meaningful; FREEZE/HOLD would legitimately sustain.
            setParam (p.getState(), layerParamId (Layer::upper, pid::delayFreeze), 0.0f);
            setParam (p.getState(), layerParamId (Layer::upper, pid::hold), 0.0f);
            p.setPlayConfigDetails (0, 2, 48000.0, 256);
            p.prepareToPlay (48000.0, 256);

            juce::AudioBuffer<float> b (2, 256);
            bool finite = true;
            for (int pos = 0; pos < 24000; pos += 256)
            {
                juce::MidiBuffer m;
                if (pos == 0)
                    for (int note : { 36, 60, 84 })
                        m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (40 + rng.nextInt (88))), 0);
                if (pos == 12032)
                    for (int note : { 36, 60, 84 })
                        m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                p.processBlock (b, m);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        finite = finite && std::isfinite (b.getSample (ch, i));
            }
            expect (finite, "trial " + juce::String (trial));
        }
    }
};

static RobustnessTests robustnessTests;
