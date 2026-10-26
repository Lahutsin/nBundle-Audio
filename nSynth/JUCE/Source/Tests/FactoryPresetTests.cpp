#include "TestHelpers.h"
#include "PluginProcessor.h"

using namespace nsynth;

#ifndef NSYNTH_FACTORY_PRESETS_DIR
 #error NSYNTH_FACTORY_PRESETS_DIR must point to the factory Presets directory
#endif

class FactoryPresetTests : public juce::UnitTest
{
public:
    FactoryPresetTests() : juce::UnitTest ("FactoryPresets", "nSynth") {}

    void runTest() override
    {
        const juce::File root (NSYNTH_FACTORY_PRESETS_DIR);

        beginTest ("seven factory categories each contain sixteen valid layer patches");
        {
            static constexpr const char* categoryNames[] = {
                "Pads", "Leads", "Bass", "Keys", "Plucks", "Brass-Strings", "FX"
            };
            expect (! root.getChildFile ("Patches").getChildFile ("Arp-Seq").exists());
            NSynthAudioProcessor processor;
            int total = 0;
            for (const auto* category : categoryNames)
            {
                const auto directory = root.getChildFile ("Patches").getChildFile (category);
                auto patches = directory.findChildFiles (juce::File::findFiles, false, "*.nsynthpatch");
                patches.sort();
                expectEquals (patches.size(), 16, category);
                for (const auto& patch : patches)
                {
                    juce::String error;
                    const bool loaded = processor.loadPatch (Layer::upper, patch, &error);
                    expect (loaded, patch.getFileName() + ": " + error);
                    if (loaded)
                    {
                        for (const auto& spec : getLayerParamSpecs())
                        {
                            auto* parameter = processor.getState().getParameter (layerParamId (Layer::upper, spec.suffix));
                            expect (parameter != nullptr && std::isfinite (parameter->getValue()),
                                    patch.getFileName() + " has a non-finite parameter");
                        }
                    }
                    ++total;
                }
            }
            expectEquals (total, 112);
        }

        beginTest ("at least sixteen factory performances are discovered and load successfully");
        {
            const auto factoryDirectory = root.getChildFile ("Performances");
            nSynth::PresetManager manager (factoryDirectory, factoryDirectory, ".nsynthperf");
            auto performances = manager.getFactoryPresets();
            expectGreaterOrEqual ((int) performances.size(), 16);

            NSynthAudioProcessor processor;
            for (const auto& performance : performances)
            {
                juce::String error;
                expect (processor.loadPerformance (performance, &error), performance.getFileName() + ": " + error);
                expectEquals (processor.getCurrentPresetName(), performance.getFileNameWithoutExtension());
            }
        }
    }
};

static FactoryPresetTests factoryPresetTests;
