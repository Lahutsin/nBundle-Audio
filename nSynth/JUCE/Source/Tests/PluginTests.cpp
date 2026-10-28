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

    static ParamControl* findControl (juce::Component& c, const juce::String& id)
    {
        if (auto* control = dynamic_cast<ParamControl*> (&c); control != nullptr && control->getParamId() == id)
            return control;
        for (auto* child : c.getChildren())
            if (auto* found = findControl (*child, id))
                return found;
        return nullptr;
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
            setParam (a.getState(), layerParamId (Layer::upper, pid::reverbDecay), 7.5f);
            setParam (a.getState(), layerParamId (Layer::lower, pid::delayDivision), (float) toInt (DelayDivision::d1_8D));
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
        }

        beginTest ("state from an older version: sequencer banks and removed parameters are ignored");
        {
            NSynthAudioProcessor a;
            setParam (a.getState(), layerParamId (Layer::upper, pid::lpf), 2345.0f);
            juce::MemoryBlock blob;
            a.getStateInformation (blob);
            auto xml = NSynthAudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            expect (xml != nullptr);
            auto legacy = juce::ValueTree::fromXml (*xml);
            juce::ValueTree sequencer ("SEQUENCER");
            sequencer.setProperty ("layer", 0, nullptr);
            legacy.addChild (sequencer, -1, nullptr);
            juce::ValueTree oldParam ("PARAM");
            oldParam.setProperty ("id", "up_arp_on", nullptr);
            oldParam.setProperty ("value", 1.0f, nullptr);
            legacy.addChild (oldParam, -1, nullptr);
            juce::MemoryBlock legacyBlob;
            NSynthAudioProcessor::copyXmlToBinary (*legacy.createXml(), legacyBlob);

            NSynthAudioProcessor b;
            b.setStateInformation (legacyBlob.getData(), (int) legacyBlob.getSize());
            expectWithinAbsoluteError (b.getState().getRawParameterValue (layerParamId (Layer::upper, pid::lpf))->load(), 2345.0f, 0.5f);
            juce::MemoryBlock resaved;
            b.getStateInformation (resaved);
            auto resavedXml = NSynthAudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
            expect (resavedXml != nullptr && resavedXml->getChildByName ("SEQUENCER") == nullptr);
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
            expectEquals ((int) state.getRawParameterValue (waveId)->load(), toInt (Dds1Wave::saw));
            expect (p.setPatchCompare (Layer::upper, false));
            expect (p.isPatchDirty (Layer::upper));
            expectWithinAbsoluteError (p.getEngine().modMatrix (Layer::upper).getFixedAmount (ModSource::ribbon, ModTarget::waveMod),
                                       -0.3f, 1.0e-7f);
            expectEquals ((int) state.getRawParameterValue (waveId)->load(), toInt (Dds1Wave::alt));

            setParam (state, cutoffId, 1200.0f);
            expect (p.loadPatch (Layer::upper, file, &error), error);
            expect (! p.isPatchDirty (Layer::upper));
            expectWithinAbsoluteError (state.getRawParameterValue (cutoffId)->load(), 812.0f, 0.5f);
            expectEquals (p.getPatchName (Layer::upper), file.getFileNameWithoutExtension());
            // Patches are layer independent: the UPPER patch loads into LOWER as well.
            setParam (state, layerParamId (Layer::lower, pid::lpf), 3000.0f);
            expect (p.loadPatch (Layer::lower, file, &error), error);
            expectWithinAbsoluteError (state.getRawParameterValue (layerParamId (Layer::lower, pid::lpf))->load(), 812.0f, 0.5f);
            expectWithinAbsoluteError (p.getEngine().modMatrix (Layer::lower).getFixedAmount (ModSource::ribbon, ModTarget::waveMod),
                                       0.3f, 1.0e-7f);
            expect (! p.isPatchDirty (Layer::lower));
            file.deleteFile();
        }

        beginTest ("patches from older versions load: sequencer data and removed parameters are skipped, new ones default");
        {
            NSynthAudioProcessor p;
            auto& state = p.getState();
            setParam (state, layerParamId (Layer::upper, pid::lpf), 640.0f);
            setParam (state, layerParamId (Layer::upper, pid::reverbMix), 0.3f);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("legacy", ".nsynthpatch");
            juce::String error;
            expect (p.savePatch (Layer::upper, file, &error), error);

            auto xml = juce::XmlDocument::parse (file);
            expect (xml != nullptr);
            auto tree = juce::ValueTree::fromXml (*xml);
            for (int i = tree.getNumChildren() - 1; i >= 0; --i)
                if (tree.getChild (i).getProperty ("id").toString() == layerParamId (Layer::upper, pid::reverbMix))
                    tree.removeChild (i, nullptr);
            juce::ValueTree sequencer ("SEQUENCER");
            sequencer.setProperty ("layer", 0, nullptr);
            tree.addChild (sequencer, -1, nullptr);
            juce::ValueTree oldParam ("PARAM");
            oldParam.setProperty ("id", "up_seq_slot", nullptr);
            oldParam.setProperty ("value", 0.4f, nullptr);
            tree.addChild (oldParam, -1, nullptr);
            expect (tree.createXml()->writeTo (file));

            setParam (state, layerParamId (Layer::upper, pid::lpf), 5000.0f);
            expect (p.loadPatch (Layer::upper, file, &error), error);
            expectWithinAbsoluteError (state.getRawParameterValue (layerParamId (Layer::upper, pid::lpf))->load(), 640.0f, 0.5f);
            expectEquals (state.getRawParameterValue (layerParamId (Layer::upper, pid::reverbMix))->load(), 0.0f);

            juce::ValueTree wrongLayer ("PARAM");
            wrongLayer.setProperty ("id", "lo_lpf", nullptr);
            wrongLayer.setProperty ("value", 0.5f, nullptr);
            tree.addChild (wrongLayer, -1, nullptr);
            expect (tree.createXml()->writeTo (file));
            expect (! p.loadPatch (Layer::upper, file, &error));
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

            // Shared cells: delay TIME in ms or as a division, static velocity or curve.
            for (auto layer : { Layer::upper, Layer::lower })
            {
                auto* ms = findControl (ed->getContent(), layerParamId (layer, pid::delayMs));
                auto* division = findControl (ed->getContent(), layerParamId (layer, pid::delayDivision));
                auto* fixed = findControl (ed->getContent(), layerParamId (layer, pid::velFixed));
                auto* curve = findControl (ed->getContent(), layerParamId (layer, pid::velCurve));
                expect (ms != nullptr && division != nullptr && fixed != nullptr && curve != nullptr);
                expect (ms->getBounds() == division->getBounds() && fixed->getBounds() == curve->getBounds());
                expect (division->isVisible() && ! ms->isVisible());      // SYNC is the default
                expect (fixed->isVisible() && ! curve->isVisible());      // STATIC is the default
                setParam (p.getState(), layerParamId (layer, pid::delaySync), 0.0f);
                setParam (p.getState(), layerParamId (layer, pid::velMode), (float) toInt (VelocityMode::dynamic));
                ed->updateSwitchedControls();
                expect (ms->isVisible() && ! division->isVisible());
                expect (curve->isVisible() && ! fixed->isVisible());
            }

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

        beginTest ("preset browser: opens from the preset display, filters, auditions, steps and keeps favourites");
        {
            NSynthAudioProcessor p;
            std::unique_ptr<NSynthAudioProcessorEditor> ed (dynamic_cast<NSynthAudioProcessorEditor*> (p.createEditor()));
            auto& browser = ed->getPresetBrowser();
            expect (! browser.isOpen());
            ed->getPresetDisplay().onClick();
            expect (browser.isOpen());
            expect (browser.getKind() == nSynth::PresetKind::patch);

            browser.setCategory ({});
            expectGreaterOrEqual (browser.getNumVisiblePresets(), 112);
            browser.setCategory ("Bass");
            expectEquals (browser.getNumVisiblePresets(), 16);
            for (int row = 0; row < browser.getNumVisiblePresets(); ++row)
                expectEquals (browser.getVisiblePreset (row)->category, juce::String ("Bass"));

            // A click on a row loads it into the target layer immediately.
            browser.setTargetLayer (Layer::lower);
            expect (browser.loadRow (2, false));
            const auto* third = browser.getVisiblePreset (2);
            expectEquals (p.getPatchKey (Layer::lower), third->key());
            expectEquals (p.getPatchName (Layer::lower), third->name);
            browser.step (1);
            expectEquals (p.getPatchKey (Layer::lower), browser.getVisiblePreset (3)->key());
            browser.step (-2);
            expectEquals (p.getPatchKey (Layer::lower), browser.getVisiblePreset (1)->key());

            browser.setCategory ({});
            browser.setSearchText ("glass");
            expectGreaterThan (browser.getNumVisiblePresets(), 0);
            for (int row = 0; row < browser.getNumVisiblePresets(); ++row)
                expect (browser.getVisiblePreset (row)->name.containsIgnoreCase ("glass"));
            browser.setSearchText ("no such preset at all");
            expectEquals (browser.getNumVisiblePresets(), 0);
            browser.setSearchText ({});

            auto& library = p.getPresetLibrary();
            const auto& entry = *browser.getVisiblePreset (0);
            const bool wasFavourite = library.isFavourite (entry);
            library.setFavourite (entry, true);
            browser.setCategory (nSynth::PresetLibrary::favouritesCategory);
            bool listed = false;
            for (int row = 0; row < browser.getNumVisiblePresets(); ++row)
                listed |= browser.getVisiblePreset (row)->key() == entry.key();
            expect (listed);
            library.setFavourite (entry, wasFavourite);

            browser.setKind (nSynth::PresetKind::performance);
            browser.setCategory ({});
            expectGreaterOrEqual (browser.getNumVisiblePresets(), 16);
            const auto firstPerformance = browser.getVisiblePreset (0)->name;
            expect (browser.loadRow (0, true));
            expect (! browser.isOpen());
            expectEquals (p.getCurrentPresetName(), firstPerformance);
            expectEquals (ed->getPresetDisplay().getPresetName(), firstPerformance);
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
