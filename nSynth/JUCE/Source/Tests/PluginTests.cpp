#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace nsynth;
using namespace nsynth::test;

class PluginTests : public juce::UnitTest
{
public:
    PluginTests() : juce::UnitTest ("Plugin", "nSynth") {}

    static int countControls (juce::Component& c)
    {
        int n = dynamic_cast<ParamControl*> (&c) != nullptr ? 1 : 0;
        for (auto* child : c.getChildren())
            n += countControls (*child);
        return n;
    }

    static int countSegmentedChoices (juce::Component& c)
    {
        int n = 0;
        if (auto* control = dynamic_cast<ParamControl*> (&c); control != nullptr && control->getChoiceButtonCount() == 3)
            ++n;
        for (auto* child : c.getChildren())
            n += countSegmentedChoices (*child);
        return n;
    }

    void runTest() override
    {
        beginTest ("state round trip restores every parameter and the editor scale");
        {
            NSynthAudioProcessor a;
            setParam (a.getState(), layerParamId (Layer::lower, pid::lpf), 1234.0f);
            setParam (a.getState(), layerParamId (Layer::upper, pid::voiceMode), (float) toInt (VoiceMode::legato));
            setParam (a.getState(), perfParamId (pid::kbdMode), (float) toInt (KeyboardMode::split));
            a.getEngine().modMatrix (Layer::upper).setFixedAmount (ModSource::lfo2, ModTarget::crossMod, 0.65f);
            SequenceStep sequenceStep;
            sequenceStep.notes = { -7, 0, 5, 0, 0, 0, 0, 0 };
            sequenceStep.noteCount = 3;
            sequenceStep.rest = false;
            sequenceStep.slide = true;
            expect (a.getEngine().sequenceBank (Layer::upper).setLength (2, 32));
            expect (a.getEngine().sequenceBank (Layer::upper).setStep (2, 10, sequenceStep));
            a.setSavedEditorScale (1.25f);
            juce::MemoryBlock blob;
            a.getStateInformation (blob);

            NSynthAudioProcessor b;
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            for (auto* p : a.getParameters())
            {
                auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
                auto* other = b.getState().getParameter (rp->getParameterID());
                expectWithinAbsoluteError (other->getValue(), p->getValue(), 1.0e-6f, rp->getParameterID());
            }
            expectEquals (b.getSavedEditorScale(), 1.25f);
            expectWithinAbsoluteError (b.getEngine().modMatrix (Layer::upper).getFixedAmount (ModSource::lfo2, ModTarget::crossMod),
                                       0.65f, 1.0e-7f);
            expect (*b.getEngine().sequenceBank (Layer::upper).snapshot()
                         == *a.getEngine().sequenceBank (Layer::upper).snapshot());
        }

        beginTest ("performance file save/load round trip");
        {
            NSynthAudioProcessor a;
            setParam (a.getState(), layerParamId (Layer::upper, pid::res), 0.77f);
            a.getEngine().modMatrix (Layer::lower).setFixedAmount (ModSource::velocity, ModTarget::lpfResonance, -0.45f);
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("perf", ".nsynthperf");
            juce::String error;
            expect (a.savePerformance (file, &error), error);
            expectEquals (a.getCurrentPresetName(), file.getFileNameWithoutExtension());

            NSynthAudioProcessor b;
            expect (b.loadPerformance (file, &error), error);
            expectWithinAbsoluteError (b.getState().getRawParameterValue (layerParamId (Layer::upper, pid::res))->load(), 0.77f, 1.0e-5f);
            expectWithinAbsoluteError (b.getEngine().modMatrix (Layer::lower).getFixedAmount (ModSource::velocity, ModTarget::lpfResonance),
                                       -0.45f, 1.0e-7f);

            auto bogus = file.getSiblingFile ("bogus.nsynthperf");
            bogus.replaceWithText ("<OTHER/>");
            expect (! b.loadPerformance (bogus, &error));
            file.deleteFile();
            bogus.deleteFile();
        }

        beginTest ("patch save/load, dirty tracking, compare restore, and layer validation");
        {
            NSynthAudioProcessor p;
            auto& state = p.getState();
            const auto waveId = layerParamId (Layer::upper, pid::dds1Wave);
            const auto cutoffId = layerParamId (Layer::upper, pid::lpf);
            setParam (state, cutoffId, 812.0f);
            p.getEngine().modMatrix (Layer::upper).setFixedAmount (ModSource::ribbon, ModTarget::waveMod, 0.3f);
            SequenceStep savedStep;
            savedStep.notes[0] = -12;
            savedStep.noteCount = 1;
            savedStep.rest = false;
            expect (p.getEngine().sequenceBank (Layer::upper).setStep (0, 0, savedStep));
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("patch", ".nsynthpatch");
            juce::String error;
            expect (p.savePatch (Layer::upper, file, &error), error);
            expect (! p.isPatchDirty (Layer::upper));

            setParam (state, waveId, (float) toInt (Dds1Wave::alt));
            p.getEngine().modMatrix (Layer::upper).setFixedAmount (ModSource::ribbon, ModTarget::waveMod, -0.3f);
            expect (p.isPatchDirty (Layer::upper));
            expect (p.setPatchCompare (Layer::upper, true));
            expect (! p.isPatchDirty (Layer::upper));
            expectWithinAbsoluteError (p.getEngine().modMatrix (Layer::upper).getFixedAmount (ModSource::ribbon, ModTarget::waveMod),
                                       0.3f, 1.0e-7f);
            expect (p.getEngine().sequenceBank (Layer::upper).snapshot()->slots[0].steps[0] == savedStep);
            expectEquals ((int) state.getRawParameterValue (waveId)->load(), toInt (Dds1Wave::saw));
            expect (p.setPatchCompare (Layer::upper, false));
            expect (p.isPatchDirty (Layer::upper));
            SequenceStep editedStep;
            editedStep.notes[0] = 7;
            editedStep.noteCount = 1;
            editedStep.rest = false;
            expect (p.getEngine().sequenceBank (Layer::upper).setStep (0, 0, editedStep));
            expect (p.isPatchDirty (Layer::upper));
            expect (p.getEngine().sequenceBank (Layer::upper).snapshot()->slots[0].steps[0] == editedStep);
            expectWithinAbsoluteError (p.getEngine().modMatrix (Layer::upper).getFixedAmount (ModSource::ribbon, ModTarget::waveMod),
                                       -0.3f, 1.0e-7f);
            expectEquals ((int) state.getRawParameterValue (waveId)->load(), toInt (Dds1Wave::alt));

            setParam (state, cutoffId, 1200.0f);
            expect (p.loadPatch (Layer::upper, file, &error), error);
            expect (! p.isPatchDirty (Layer::upper));
            expect (p.getEngine().sequenceBank (Layer::upper).snapshot()->slots[0].steps[0] == savedStep);
            expectWithinAbsoluteError (state.getRawParameterValue (cutoffId)->load(), 812.0f, 0.5f);
            expect (! p.loadPatch (Layer::lower, file, &error));
            expectEquals (p.getPatchName (Layer::upper), file.getFileNameWithoutExtension());
            file.deleteFile();
        }

        beginTest ("performance load preserves master volume");
        {
            NSynthAudioProcessor p;
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("perf-master", ".nsynthperf");
            juce::String error;
            setParam (p.getState(), perfParamId (pid::masterVolume), -12.0f);
            expect (p.savePerformance (file, &error), error);
            setParam (p.getState(), perfParamId (pid::masterVolume), -3.0f);
            const float liveMaster = p.getState().getRawParameterValue (perfParamId (pid::masterVolume))->load();
            expect (p.loadPerformance (file, &error), error);
            expectWithinAbsoluteError (p.getState().getRawParameterValue (perfParamId (pid::masterVolume))->load(), liveMaster, 1.0e-6f);
            file.deleteFile();
        }

        beginTest ("patch embeds user wavetable and Compare restores both waveform versions");
        {
            NSynthAudioProcessor processor;
            constexpr double sampleRate = 48000.0;
            processor.prepareToPlay (sampleRate, 64);
            auto& engine = processor.getEngine();
            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer midi;
            auto pumpImport = [&]
            {
                for (int attempt = 0; attempt < 2000; ++attempt)
                {
                    const auto state = engine.getWaveImportState();
                    if (state == SynthEngine::WaveImportState::ready)
                        return true;
                    if (state == SynthEngine::WaveImportState::failed)
                        return false;
                    engine.process (audio, nullptr, nullptr, midi, 120.0);
                    juce::Thread::sleep (1);
                }
                return false;
            };

            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("nsynth_patch_wave", ".ws6");
            auto patch = file.withFileExtension ("nsynthpatch");
            std::vector<float> cycle (MipWavetable::kTableSize);
            juce::String error;
            for (int n = 0; n < MipWavetable::kTableSize; ++n)
                cycle[(size_t) n] = (float) std::sin (juce::MathConstants<double>::twoPi * n / MipWavetable::kTableSize);
            expect (WaveBank::writeWs6 (file, cycle, &error), error);
            expect (engine.requestWaveImport (3, file, &error), error);
            expect (pumpImport(), engine.getWaveImportError());
            expect (processor.savePatch (Layer::upper, patch, &error), error);

            for (int n = 0; n < MipWavetable::kTableSize; ++n)
                cycle[(size_t) n] = (float) std::sin (juce::MathConstants<double>::twoPi * 2.0 * n / MipWavetable::kTableSize);
            expect (WaveBank::writeWs6 (file, cycle, &error), error);
            expect (engine.requestWaveImport (3, file, &error), error);
            expect (pumpImport(), engine.getWaveImportError());
            expect (processor.isPatchDirty (Layer::upper));

            expect (processor.setPatchCompare (Layer::upper, true));
            expect (pumpImport(), engine.getWaveImportError());
            expect (! processor.isPatchDirty (Layer::upper));
            expectWithinAbsoluteError (engine.getWaveBank().alt (3).read (0, 0.25), 1.0f, 0.01f);
            expect (processor.setPatchCompare (Layer::upper, false));
            expect (pumpImport(), engine.getWaveImportError());
            expect (processor.isPatchDirty (Layer::upper));
            expectWithinAbsoluteError (engine.getWaveBank().alt (3).read (0, 0.25),
                                      0.0f, 0.01f);
            file.deleteFile();
            patch.deleteFile();
        }

        beginTest ("INIT resets a layer, COPY duplicates a layer");
        {
            NSynthAudioProcessor p;
            auto& s = p.getState();
            setParam (s, layerParamId (Layer::upper, pid::dds1Wave), (float) toInt (Dds1Wave::alt));
            setParam (s, layerParamId (Layer::upper, pid::lpf), 500.0f);
            p.copyLayer (Layer::upper, Layer::lower);
            expectEquals ((int) s.getRawParameterValue (layerParamId (Layer::lower, pid::dds1Wave))->load(), toInt (Dds1Wave::alt));
            expectWithinAbsoluteError (s.getRawParameterValue (layerParamId (Layer::lower, pid::lpf))->load(), 500.0f, 0.5f);
            p.initLayer (Layer::upper);
            expectEquals ((int) s.getRawParameterValue (layerParamId (Layer::upper, pid::dds1Wave))->load(), toInt (Dds1Wave::saw));
            expectEquals ((int) s.getRawParameterValue (layerParamId (Layer::lower, pid::dds1Wave))->load(), toInt (Dds1Wave::alt));
        }

        beginTest ("editor: every parameter has a control; uniform scaling with fixed aspect ratio and limits");
        {
            NSynthAudioProcessor p;
            std::unique_ptr<NSynthAudioProcessorEditor> ed (dynamic_cast<NSynthAudioProcessorEditor*> (p.createEditor()));
            expect (ed != nullptr);
            expectEquals (countControls (ed->getContent()), 2 * (int) getLayerParamSpecs().size() + (int) getPerformanceParamSpecs().size());
            expectGreaterThan (countSegmentedChoices (ed->getContent()), 0);
            expect (! ed->getModMatrixPage().isVisible());
            ed->showModMatrixPage (true);
            expect (ed->getModMatrixPage().isVisible());
            ed->getModMatrixPage().setLayer (Layer::lower);
            ed->showModMatrixPage (false);
            expect (! ed->getWaveBrowserPage().isVisible());
            ed->showWaveBrowserPage (true);
            expect (ed->getWaveBrowserPage().isVisible());
            ed->showWaveBrowserPage (false);
            expect (! ed->getSequencePage().isVisible());
            ed->showSequencePage (true);
            expect (ed->getSequencePage().isVisible());
            ed->getSequencePage().setLayer (Layer::lower);
            ed->showSequencePage (false);

            for (float s : { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f })
            {
                ed->setScale (s);
                expectEquals (ed->getWidth(), juce::roundToInt (NSynthAudioProcessor::kDesignWidth * s));
                expectEquals (ed->getHeight(), juce::roundToInt (NSynthAudioProcessor::kDesignHeight * s));
                expectWithinAbsoluteError (ed->getScale(), s, 1.0e-4f);
                const auto t = ed->getContent().getTransform();
                expectWithinAbsoluteError (t.mat00, t.mat11, 1.0e-6f); // same factor on both axes
                expectWithinAbsoluteError (p.getSavedEditorScale(), s, 1.0e-4f);
            }
            ed->setScale (5.0f);
            expectEquals (ed->getWidth(), NSynthAudioProcessor::kDesignWidth * 2);
            ed->setScale (0.1f);
            expectEquals (ed->getWidth(), NSynthAudioProcessor::kDesignWidth / 2);

            auto* c = ed->getConstrainer();
            expect (c != nullptr);
            expectWithinAbsoluteError (c->getFixedAspectRatio(),
                                       (double) NSynthAudioProcessor::kDesignWidth / (double) NSynthAudioProcessor::kDesignHeight, 1.0e-9);
            expectEquals (c->getMinimumWidth(), NSynthAudioProcessor::kDesignWidth / 2);
            expectEquals (c->getMaximumWidth(), NSynthAudioProcessor::kDesignWidth * 2);
            ed.reset();
        }

        beginTest ("editor reopens at the saved scale");
        {
            NSynthAudioProcessor p;
            p.setSavedEditorScale (1.25f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            expectEquals (ed->getWidth(), juce::roundToInt (NSynthAudioProcessor::kDesignWidth * 1.25f));
        }
    }
};

static PluginTests pluginTests;
