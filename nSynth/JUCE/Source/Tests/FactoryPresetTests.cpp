#include "TestHelpers.h"
#include "PluginProcessor.h"

using namespace nsynth;
using namespace nsynth::test;
using nSynth::PresetKind;

#ifndef NSYNTH_FACTORY_PRESETS_DIR
 #error NSYNTH_FACTORY_PRESETS_DIR must point to the factory Presets directory
#endif

class FactoryPresetTests : public juce::UnitTest
{
public:
    FactoryPresetTests() : juce::UnitTest ("FactoryPresets", "nSynth") {}

    static std::vector<float> renderChord (NSynthAudioProcessor& processor, int samples)
    {
        processor.setPlayConfigDetails (0, 2, 48000.0, 256);
        processor.prepareToPlay (48000.0, 256);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos = 0; pos < samples; pos += 256)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
            processor.processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
                out.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));
        }
        return out;
    }

    void runTest() override
    {
        const juce::File root (NSYNTH_FACTORY_PRESETS_DIR);
        static constexpr const char* categoryNames[] = {
            "Pads", "Leads", "Bass", "Keys", "Plucks", "Brass & Strings", "FX"
        };

        beginTest ("the embedded factory library matches the Presets folder: 7 x 16 patches, 16 performances");
        {
            NSynthAudioProcessor processor;
            auto& library = processor.getPresetLibrary();
            const auto categories = library.getCategories (PresetKind::patch);
            for (const auto* categoryName : categoryNames)
            {
                expect (categories.contains (categoryName), categoryName);
                int factoryCount = 0;
                for (int index : library.filter (PresetKind::patch, categoryName, {}))
                    factoryCount += library.getEntries()[(size_t) index].factory ? 1 : 0;
                expectEquals (factoryCount, 16, categoryName);
            }

            int patches = 0, performances = 0;
            for (const auto& entry : library.getEntries())
                if (entry.factory)
                    (entry.kind == PresetKind::patch ? patches : performances)++;
            expectEquals (patches, 112);
            expectEquals (performances, 16);
            expectEquals (root.findChildFiles (juce::File::findFiles, true, "*.nsynthpatch").size(), 112);
            expectEquals (root.findChildFiles (juce::File::findFiles, true, "*.nsynthperf").size(), 16);
        }

        beginTest ("every factory patch loads into both layers with finite values and is not dirty afterwards");
        {
            NSynthAudioProcessor processor;
            auto& library = processor.getPresetLibrary();
            for (const auto& entry : library.getEntries())
            {
                if (! entry.factory || entry.kind != PresetKind::patch)
                    continue;
                expect (entry.name.isNotEmpty() && ! entry.name.containsChar ('_'), entry.name);
                for (auto layer : { Layer::upper, Layer::lower })
                {
                    juce::String error;
                    const bool loaded = processor.loadPatch (layer, entry, &error);
                    expect (loaded, entry.name + ": " + error);
                    if (! loaded)
                        continue;
                    expectEquals (processor.getPatchName (layer), entry.name);
                    expectEquals (processor.getPatchCategory (layer), entry.category);
                    expectEquals (processor.getPatchKey (layer), entry.key());
                    expect (! processor.isPatchDirty (layer), entry.name + " is dirty after loading");
                    for (const auto& spec : getLayerParamSpecs())
                    {
                        auto* parameter = processor.getState().getParameter (layerParamId (layer, spec.suffix));
                        expect (parameter != nullptr && std::isfinite (parameter->getValue()),
                                entry.name + " has a non-finite parameter");
                    }
                }
            }
        }

        beginTest ("every factory performance loads and shows its display name");
        {
            NSynthAudioProcessor processor;
            for (const auto& entry : processor.getPresetLibrary().getEntries())
            {
                if (! entry.factory || entry.kind != PresetKind::performance)
                    continue;
                juce::String error;
                expect (processor.loadPerformance (entry, &error), entry.name + ": " + error);
                expectEquals (processor.getCurrentPresetName(), entry.name);
                expectEquals (processor.getPerformanceKey(), entry.key());
                expect (entry.category == "Layers" || entry.category == "Splits", entry.category);
            }
        }

        beginTest ("factory patches sound: audible, finite and below full scale");
        {
            NSynthAudioProcessor scanner;
            auto& library = scanner.getPresetLibrary();
            for (const auto* categoryName : categoryNames)
            {
                // First patch of every category (rendering all of them takes too long for a unit test).
                const auto indices = library.filter (PresetKind::patch, categoryName, {});
                expect (! indices.empty());
                if (indices.empty())
                    continue;
                const auto& entry = library.getEntries()[(size_t) indices.front()];
                NSynthAudioProcessor processor;
                expect (processor.loadPatch (Layer::upper, entry));
                const auto out = renderChord (processor, 48000);
                expect (allFinite (out), entry.name);
                expectGreaterThan (toDb (rms (out, 4800)), -40.0, entry.name);
                expectLessThan (peak (out), 1.0f, entry.name);
            }
        }

        beginTest ("legacy file API still loads the factory files from disk");
        {
            const auto factoryDirectory = root.getChildFile ("Performances");
            nSynth::PresetManager manager (factoryDirectory, factoryDirectory, ".nsynthperf");
            auto performances = manager.getFactoryPresets();
            expectEquals ((int) performances.size(), 16);
            NSynthAudioProcessor processor;
            for (const auto& performance : performances)
            {
                juce::String error;
                expect (processor.loadPerformance (performance, &error), performance.getFileName() + ": " + error);
            }
            for (const auto& patch : root.getChildFile ("Patches").findChildFiles (juce::File::findFiles, true, "*.nsynthpatch"))
            {
                juce::String error;
                expect (processor.loadPatch (Layer::lower, patch, &error), patch.getFileName() + ": " + error);
            }
        }
    }
};

static FactoryPresetTests factoryPresetTests;
