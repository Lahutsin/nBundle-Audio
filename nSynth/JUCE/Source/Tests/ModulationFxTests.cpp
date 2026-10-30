#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "Core/DSP/Filter2.h"
#include "Core/DSP/Effects.h"

using namespace nsynth;
using namespace nsynth::test;

// FILTER 2, the version 2 effect modules, and the modulation slots / effect chain in the engine.
class ModulationFxTests : public juce::UnitTest
{
public:
    ModulationFxTests() : juce::UnitTest ("Modulation and FX", "nSynth") {}

    static constexpr double voiceRate = 96000.0, hostRate = 48000.0;

    static double filter2Gain (Filter2Type type, double freq, float cutoff, float res = 0.0f)
    {
        Filter2 f;
        f.setSampleRate (voiceRate);
        const int n = (int) voiceRate / 2;
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = f.process (0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / voiceRate),
                                         type, cutoff, res, 0.0f);
        return toneAmplitude (out, freq, voiceRate, (size_t) n / 2) / 0.1;
    }

    static std::vector<float> channel (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        std::vector<float> v;
        for (int i = from; i < to; ++i)
            v.push_back (b.getSample (ch, i));
        return v;
    }

    static void fillSine (juce::AudioBuffer<float>& b, double freq, float amplitude)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / hostRate);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }

    struct Rig
    {
        Rig()
        {
            proc.setRateAndBufferSizeDetails (hostRate, 256);
            proc.prepareToPlay (hostRate, 256);
        }
        void setLayer (Layer l, const char* suffix, float v) { setParam (proc.getState(), layerParamId (l, suffix), v); }
        void setPerf (const char* suffix, float v) { setParam (proc.getState(), perfParamId (suffix), v); }
        ModMatrix& matrix (Layer l = Layer::upper) { return proc.getEngine().modMatrix (l); }

        // Main mix (and the UPPER layer output) with MIDI at absolute sample positions.
        std::pair<std::vector<float>, std::vector<float>> render (int numSamples, const std::vector<std::pair<int, juce::MidiMessage>>& events = {})
        {
            std::vector<float> mixOut, upperOut;
            juce::AudioBuffer<float> mix (2, 256), upper (2, 256);
            for (int pos = 0; pos < numSamples; pos += 256)
            {
                const int n = juce::jmin (256, numSamples - pos);
                juce::AudioBuffer<float> mixView (mix.getArrayOfWritePointers(), 2, n), upperView (upper.getArrayOfWritePointers(), 2, n);
                juce::MidiBuffer midi;
                for (auto& [t, m] : events)
                    if (t >= pos && t < pos + n)
                        midi.addEvent (m, t - pos);
                proc.getEngine().process (mixView, &upperView, nullptr, midi, 120.0);
                for (int i = 0; i < n; ++i)
                {
                    mixOut.push_back (mixView.getSample (0, i));
                    upperOut.push_back (upperView.getSample (0, i));
                }
            }
            return { mixOut, upperOut };
        }
        NSynthAudioProcessor proc;
    };

    static juce::MidiMessage on (int note, int channel = 1) { return juce::MidiMessage::noteOn (channel, note, 1.0f); }
    static juce::MidiMessage off (int note, int channel = 1) { return juce::MidiMessage::noteOff (channel, note); }

    void runTest() override
    {
        beginTest ("FILTER 2: slopes of the low-, high-, band-pass and notch responses");
        {
            expectWithinAbsoluteError (toDb (filter2Gain (Filter2Type::lp12, 100.0, 1000.0f)), 0.0, 1.0);
            expectLessThan (toDb (filter2Gain (Filter2Type::lp12, 8000.0, 1000.0f)), -30.0);
            expectLessThan (toDb (filter2Gain (Filter2Type::lp24, 8000.0, 1000.0f)), -60.0);
            expectWithinAbsoluteError (toDb (filter2Gain (Filter2Type::hp12, 8000.0, 1000.0f)), 0.0, 1.0);
            expectLessThan (toDb (filter2Gain (Filter2Type::hp24, 125.0, 1000.0f)), -60.0);
            expectWithinAbsoluteError (toDb (filter2Gain (Filter2Type::bandpass, 1000.0, 1000.0f, 0.5f)), 0.0, 1.0);
            expectLessThan (toDb (filter2Gain (Filter2Type::bandpass, 125.0, 1000.0f, 0.5f)), -15.0);
            expectLessThan (toDb (filter2Gain (Filter2Type::notch, 1000.0, 1000.0f)), -20.0);
            expectWithinAbsoluteError (toDb (filter2Gain (Filter2Type::notch, 60.0, 1000.0f)), 0.0, 1.0);
            // Resonance raises the peak at the cutoff.
            expectGreaterThan (filter2Gain (Filter2Type::lp12, 1000.0, 1000.0f, 0.9f), 3.0);
        }

        beginTest ("FILTER 2: comb echoes at the cutoff period, formants follow the vowel table");
        {
            Filter2 f;
            f.setSampleRate (voiceRate);
            std::vector<float> out (4000);
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = f.process (i == 0 ? 1.0f : 0.0f, Filter2Type::combPlus, 480.0f, 0.9f, 0.0f);
            const size_t period = (size_t) (voiceRate / 480.0);   // 200 samples
            expectGreaterThan (std::abs (out[period]) + std::abs (out[period + 1]), 0.3f);
            expectLessThan (std::abs (out[period / 2]), 1.0e-6f);

            const auto a = Filter2::formantsFor (0.0f);
            const auto u = Filter2::formantsFor (4.0f);
            expectWithinAbsoluteError (a[0], 800.0f, 0.5f);
            expectWithinAbsoluteError (u[1], 700.0f, 0.5f);
            const auto between = Filter2::formantsFor (0.5f);
            expect (between[0] < a[0] && between[0] > 400.0f);
        }

        beginTest ("FILTER 2: every type stays finite with full resonance, drive and a swept cutoff");
        for (int type = 0; type <= toInt (Filter2Type::formant); ++type)
        {
            Filter2 f;
            f.setSampleRate (voiceRate);
            juce::Random rng (type + 3);
            float peakOut = 0.0f;
            bool finite = true;
            for (int i = 0; i < 96000; ++i)
            {
                const float cutoff = 20.0f * std::pow (1000.0f, 0.5f + 0.5f * std::sin ((float) i * 0.0007f));
                const float y = f.process (rng.nextFloat() * 2.0f - 1.0f, (Filter2Type) type, cutoff, 1.0f, 1.0f);
                finite &= std::isfinite (y);
                peakOut = juce::jmax (peakOut, std::abs (y));
            }
            expect (finite, juce::String (type));
            expectLessThan (peakOut, 40.0f, juce::String (type));
        }

        beginTest ("EQ: the curve the editor draws is the one processed");
        {
            ThreeBandEq::Settings s;
            s.midDb = 6.0f;
            s.midHz = 1000.0f;
            expectWithinAbsoluteError (ThreeBandEq::responseDb (s, 1000.0, hostRate), 6.0f, 0.1f);
            expectWithinAbsoluteError (ThreeBandEq::responseDb ({}, 3000.0, hostRate), 0.0f, 1.0e-3f);
            ThreeBandEq eq;
            eq.prepare (hostRate);
            juce::AudioBuffer<float> b (2, 24000);
            fillSine (b, 1000.0, 0.1f);
            eq.process (b, 0, b.getNumSamples(), s);
            expectWithinAbsoluteError (toDb (toneAmplitude (channel (b, 0, 12000, 24000), 1000.0, hostRate) / 0.1), 6.0, 0.3);
        }

        beginTest ("EQ: every band has a Q; the shelves' default is the former slope S = 1");
        {
            ThreeBandEq::Settings smooth;
            smooth.lowDb = 9.0f;
            smooth.highDb = -9.0f;
            // RBJ shelf, S = 1 (no overshoot): half the gain at the corner, the full gain far from it.
            expectWithinAbsoluteError (ThreeBandEq::responseDb (smooth, 150.0, hostRate), 4.5f, 0.2f);
            expectWithinAbsoluteError (ThreeBandEq::responseDb (smooth, 20.0, hostRate), 9.0f, 0.3f);
            float maxSmooth = -100.0f;
            for (double hz = 20.0; hz < 1000.0; hz *= 1.02)
                maxSmooth = juce::jmax (maxSmooth, ThreeBandEq::responseDb (smooth, hz, hostRate));
            expectLessThan (maxSmooth, 9.1f);

            // A resonant shelf overshoots its gain and dips on the other side of the corner.
            auto resonant = smooth;
            resonant.lowQ = 3.0f;
            resonant.highQ = 3.0f;
            float maxLow = -100.0f, minLow = 100.0f, minHigh = 100.0f;
            for (double hz = 20.0; hz < 1000.0; hz *= 1.02)
            {
                const float db = ThreeBandEq::responseDb (resonant, hz, hostRate);
                maxLow = juce::jmax (maxLow, db);
                minLow = juce::jmin (minLow, db);
            }
            for (double hz = 1500.0; hz < 18000.0; hz *= 1.02)
                minHigh = juce::jmin (minHigh, ThreeBandEq::responseDb (resonant, hz, hostRate));
            expectGreaterThan (maxLow, 11.0f);
            expectLessThan (minLow, -0.5f);
            expectLessThan (minHigh, -11.0f);
        }

        beginTest ("compressor: static curve, gain reduction, and the key of the other layer");
        {
            Compressor::Settings s;   // -18 dB, 4:1
            expectWithinAbsoluteError (Compressor::staticCurveDb (-6.0f, s), -9.0f, 1.0e-4f);
            expectWithinAbsoluteError (Compressor::staticCurveDb (-30.0f, s), 0.0f, 1.0e-6f);

            Compressor c;
            c.prepare (hostRate);
            juce::AudioBuffer<float> b (2, 24000);
            fillSine (b, 500.0, 0.5f);
            c.process (b, 0, b.getNumSamples(), s, nullptr);
            expectWithinAbsoluteError (toDb (peak (channel (b, 0, 18000, 24000)) / 0.5), -9.0, 1.5);

            // Quiet signal, loud key: ducked. Silent key: untouched.
            juce::AudioBuffer<float> key (2, 24000), quiet (2, 24000);
            fillSine (key, 60.0, 0.9f);
            fillSine (quiet, 500.0, 0.05f);
            c.reset();
            c.process (quiet, 0, quiet.getNumSamples(), s, &key);
            expectLessThan (peak (channel (quiet, 0, 18000, 24000)), 0.03f);
            key.clear();
            fillSine (quiet, 500.0, 0.05f);
            c.reset();
            c.process (quiet, 0, quiet.getNumSamples(), s, &key);
            expectWithinAbsoluteError (peak (channel (quiet, 0, 18000, 24000)), 0.05f, 1.0e-3f);
        }

        beginTest ("distortion, phaser, flanger: bounded, finite, MIX 0 is transparent");
        {
            for (int type = 0; type <= toInt (DistortionType::crush); ++type)
            {
                Distortion d;
                d.prepare (hostRate);
                juce::AudioBuffer<float> b (2, 9600);
                fillSine (b, 220.0, 0.8f);
                d.process (b, 0, b.getNumSamples(), { (DistortionType) type, 1.0f, 1.0f, 1.0f });
                const auto out = channel (b, 0, 0, 9600);
                expect (allFinite (out), juce::String (type));
                expectLessThan (peak (out), 1.5f, juce::String (type));
                expectGreaterThan (rms (out), 0.05, juce::String (type));
            }
            juce::AudioBuffer<float> ref (2, 4800), b (2, 4800);
            fillSine (ref, 330.0, 0.5f);
            Phaser phaser;
            phaser.prepare (hostRate);
            b.makeCopyOf (ref);
            phaser.process (b, 0, b.getNumSamples(), { 1.0f, 1.0f, 0.9f, 0.0f });
            expectWithinAbsoluteError (b.getSample (1, 1000), ref.getSample (1, 1000), 1.0e-6f);
            Flanger flanger;
            flanger.prepare (hostRate, 512);
            b.makeCopyOf (ref);
            flanger.process (b, 0, b.getNumSamples(), { 1.0f, 1.0f, -0.9f, 0.0f });
            expectWithinAbsoluteError (b.getSample (0, 2000), ref.getSample (0, 2000), 1.0e-6f);
            flanger.process (b, 0, b.getNumSamples(), { 3.0f, 1.0f, 0.95f, 0.5f });
            expect (allFinite (channel (b, 0, 0, 4800)));
        }

        beginTest ("delay DUCK lowers the echoes while the dry signal plays");
        {
            auto echo = [] (float duck)
            {
                StereoDelay d;
                d.prepare (hostRate, 512);
                juce::AudioBuffer<float> b (2, 24000);
                fillSine (b, 300.0, 0.5f);
                StereoDelay::Settings s;
                s.timeSeconds = 0.1;
                s.feedback = 0.5f;
                s.mix = 1.0f;     // wet only
                s.lowCutHz = 20.0f;
                s.highCutHz = 20000.0f;
                s.duck = duck;
                d.process (b, 0, b.getNumSamples(), s);
                return rms (channel (b, 0, 12000, 24000));
            };
            expectLessThan (echo (1.0f), echo (0.0f) * 0.1);
        }

        beginTest ("engine: FILTER 2 in series removes what its high-pass cuts");
        {
            Rig plain, filtered;
            filtered.setLayer (Layer::upper, pid::filter2Routing, (float) toInt (Filter2Routing::serial));
            filtered.setLayer (Layer::upper, pid::filter2Type, (float) toInt (Filter2Type::hp24));
            filtered.setLayer (Layer::upper, pid::filter2Cutoff, 6000.0f);
            const auto a = plain.render (9600, { { 0, on (45) } }).first;
            const auto b = filtered.render (9600, { { 0, on (45) } }).first;
            expectLessThan (rms (b, 2400), rms (a, 2400) * 0.25);
        }

        beginTest ("engine: an MSEG slot shapes a voice parameter over time");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::mseg1Mode, (float) toInt (MsegMode::loop));
            rig.setLayer (Layer::upper, pid::mseg1Sync, 0.0f);
            rig.setLayer (Layer::upper, pid::mseg1Time, 0.2f);
            rig.matrix().setMseg (0, MsegShape::make (MsegShape::Preset::square));   // 1 for 0.1 s, then 0
            rig.matrix().addRoute (ModSource::mseg1, findLayerSpecIndex (pid::vcaEnv), -1.0f);
            const auto out = rig.render (19200, { { 0, on (60) } }).first;
            // 0.1 s muted (1 x -100 %), 0.1 s at full level, repeating (edges smoothed over ~4 ms).
            auto level = [&out] (int from, int to) { return rms (std::vector<float> (out.begin() + from, out.begin() + to)); };
            const double full = level (5800, 9000);
            expectGreaterThan (full, 1.0e-2);
            expectLessThan (level (1000, 4000), full * 0.005);
            expectLessThan (level (10600, 13600), full * 0.005);
            expectGreaterThan (level (15400, 18600), full * 0.5);
            expect (rig.proc.getEngine().layer (Layer::upper).getMsegPosition (0) >= 0.0f);
        }

        beginTest ("engine: MSEG 12 of a patch with 12 MSEGs works like MSEG 1");
        {
            Rig rig;
            while (rig.matrix().getMsegCount() < 12)
                rig.matrix().addMseg();
            const int m = 11;
            rig.setLayer (Layer::upper, msegSuffix (m, MsegField::mode), (float) toInt (MsegMode::loop));
            rig.setLayer (Layer::upper, msegSuffix (m, MsegField::sync), 0.0f);
            rig.setLayer (Layer::upper, msegSuffix (m, MsegField::time), 0.2f);
            rig.matrix().setMseg (m, MsegShape::make (MsegShape::Preset::square));
            rig.matrix().addRoute (msegSource (m), findLayerSpecIndex (pid::vcaEnv), -1.0f);
            const auto out = rig.render (19200, { { 0, on (60) } }).first;
            auto level = [&out] (int from, int to) { return rms (std::vector<float> (out.begin() + from, out.begin() + to)); };
            const double full = level (5800, 9000);
            expectGreaterThan (full, 1.0e-2);
            expectLessThan (level (1000, 4000), full * 0.005);
            expectLessThan (level (10600, 13600), full * 0.005);
            expect (rig.proc.getEngine().layer (Layer::upper).getMsegPosition (m) >= 0.0f);
        }

        beginTest ("engine: a macro slot drives an effect parameter without a note playing");
        {
            Rig dry, wet;
            for (auto* rig : { &dry, &wet })
            {
                rig->setLayer (Layer::upper, pid::reverbMix, 0.0f);
                rig->matrix().addRoute (ModSource::macro1, findLayerSpecIndex (pid::reverbMix), 0.6f);
            }
            wet.setLayer (Layer::upper, pid::macro1, 1.0f);
            const auto a = dry.render (48000, { { 0, on (60) }, { 4800, off (60) } }).first;
            const auto b = wet.render (48000, { { 0, on (60) }, { 4800, off (60) } }).first;
            // The tail after the release only exists with the macro up.
            expectLessThan (rms (a, 38000), 1.0e-5);
            expectGreaterThan (rms (b, 38000), 1.0e-4);
            expectWithinAbsoluteError (wet.proc.getEngine().layer (Layer::upper).getModulationOffset (findLayerSpecIndex (pid::reverbMix)),
                                       0.6f, 1.0e-4f);
        }

        beginTest ("engine: the effect order is the processing order");
        {
            Rig first, last;
            for (auto* rig : { &first, &last })
            {
                rig->setLayer (Layer::upper, pid::distOn, 1.0f);
                rig->setLayer (Layer::upper, pid::distDrive, 1.0f);
                rig->setLayer (Layer::upper, pid::distType, (float) toInt (DistortionType::hard));
                rig->setLayer (Layer::upper, pid::reverbMix, 0.5f);
            }
            last.matrix().moveFxModule (7, 0);   // reverb into the distortion
            const auto a = first.render (24000, { { 0, on (48) }, { 2400, off (48) } }).first;
            const auto b = last.render (24000, { { 0, on (48) }, { 2400, off (48) } }).first;
            double difference = 0.0;
            for (size_t i = 12000; i < a.size(); ++i)
                difference += std::abs (a[i] - b[i]);
            expectGreaterThan (difference / 12000.0, 1.0e-3);
            expect (allFinite (a) && allFinite (b));
        }

        beginTest ("engine: COMP KEY Other Layer ducks UPPER while LOWER plays");
        {
            Rig rig;
            rig.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::split));
            rig.setLayer (Layer::upper, pid::compOn, 1.0f);
            rig.setLayer (Layer::upper, pid::compKey, (float) toInt (CompressorKey::otherLayer));
            rig.setLayer (Layer::upper, pid::compThreshold, -40.0f);
            rig.setLayer (Layer::upper, pid::compRatio, 20.0f);
            rig.setLayer (Layer::upper, pid::compAttack, 1.0f);
            const auto alone = rig.render (24000, { { 0, on (72) } }).second;
            Rig keyed;
            keyed.setPerf (pid::kbdMode, (float) toInt (KeyboardMode::split));
            for (auto [suffix, value] : { std::pair { pid::compOn, 1.0f }, std::pair { pid::compThreshold, -40.0f },
                                          std::pair { pid::compRatio, 20.0f }, std::pair { pid::compAttack, 1.0f },
                                          std::pair { pid::compKey, (float) toInt (CompressorKey::otherLayer) } })
                keyed.setLayer (Layer::upper, suffix, value);
            const auto ducked = keyed.render (24000, { { 0, on (72) }, { 0, on (36) } }).second;
            expectLessThan (rms (ducked, 12000), rms (alone, 12000) * 0.5);
            expectGreaterThan (rms (alone, 12000), 1.0e-3);
        }

        beginTest ("engine: switching an effect off fades it out and resets it");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::delayMix, 1.0f);
            rig.setLayer (Layer::upper, pid::delayFeedback, 0.9f);
            rig.render (24000, { { 0, on (60) }, { 2400, off (60) } });
            rig.setLayer (Layer::upper, pid::delayOn, 0.0f);
            const auto out = rig.render (4800).first;
            expect (allFinite (out));
            expectLessThan (rms (out, 2400), 1.0e-6);   // the echoes are gone after the 10 ms fade
        }
    }
};

static ModulationFxTests modulationFxTests;
