#include "PluginProcessor.h"
#include <array>
#include <iostream>

using namespace nsynth;

namespace
{
struct Category
{
    const char* directory;
    std::array<const char*, 16> names;
};

const std::array<Category, 7> categories {{
    { "Pads", { "Afterglow", "Blue Cathedral", "Cloud Atlas", "Distant Shore",
                "Echowood", "Frosted Glass", "Gravity Well", "Hollow Moon",
                "Indigo Current", "Jade Horizon", "Keystone", "Lumen Field",
                "Memory Foam", "Northern Veil", "Open Sky", "Quiet Machine" } },
    { "Leads", { "Arc Runner", "Brightwire", "Copperline", "Daybreak",
                 "Edgewalker", "Fast Forward", "Gold Signal", "High Voltage",
                 "Ion Trail", "Jetstream", "Kinetic", "Laser Bloom",
                 "Midnight Call", "Neon Arc", "Overtone", "Pulse Driver" } },
    { "Bass", { "Anchor Point", "Black Current", "Concrete", "Deep Marker",
                "Earthline", "Floorboard", "Gravel Road", "Heavy Weather",
                "Iron Bloom", "Jet Black", "Low Orbit", "Moraine",
                "Night Shift", "Oil Drum", "Pressure Drop", "Quarry" } },
    { "Keys", { "Amber Keys", "Bellwether", "Candlelight", "Dusk Piano",
                "Electric Linen", "Felt Room", "Greenhouse", "Honeycomb",
                "Ivory Circuit", "Juniper", "Keyframe", "Little Hours",
                "Marble Hall", "Nylon Sky", "Opal Keys", "Paper Lantern" } },
    { "Plucks", { "Apricot Wire", "Bamboo Glass", "Cedar Spark", "Dry River",
                  "Elasticity", "Falling Pin", "Gossamer", "Harp Engine",
                  "Icicle", "Jupiter String", "Koto Echo", "Lightwell",
                  "Mallet Study", "Needlepoint", "Orbit Pluck", "Petal Fall" } },
    { "Brass-Strings", { "Alto Horizon", "Brass Lantern", "Cobalt Ensemble", "Dawn Section",
                         "Evergreen Bow", "Film Score", "Golden Fanfare", "Highland Air",
                         "Ivory Section", "Juniper Brass", "Kindred", "Long Arc",
                         "Majestic Room", "New Century", "Open Reeds", "Velvet Bow" } },
    { "FX", { "Aether Engine", "Broken Compass", "Clockwork Tide", "Drift Signal",
              "Eventide Wire", "Faultline", "Glass Weather", "Harmonic Dust",
              "Interference", "Junction", "Kepler Drift", "Lunar Debris",
              "Magnetic North", "Null Garden", "Orbit Decay", "Phase Bloom" } }
}};

juce::String paramId (Layer layer, const char* suffix)
{
    return layerParamId (layer, suffix);
}

void setPlain (NSynthAudioProcessor& processor, Layer layer, const char* suffix, float value)
{
    auto* parameter = processor.getState().getParameter (paramId (layer, suffix));
    if (parameter == nullptr)
        throw std::runtime_error ("Missing layer parameter: " + std::string (suffix));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void setPerformance (NSynthAudioProcessor& processor, const char* suffix, float value)
{
    auto* parameter = processor.getState().getParameter (perfParamId (suffix));
    if (parameter == nullptr)
        throw std::runtime_error ("Missing performance parameter: " + std::string (suffix));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float varied (int index, float base, float spread, float minimum, float maximum)
{
    const float offset = ((float) (index % 8) - 3.5f) / 3.5f;
    return juce::jlimit (minimum, maximum, base + offset * spread);
}

void configureLayer (NSynthAudioProcessor& processor, Layer layer, int category, int variation)
{
    processor.initLayer (layer);
    const float v = (float) (variation % 16) / 15.0f;

    setPlain (processor, layer, pid::binaural, (float) toInt (category == 2 || category == 3 ? BinauralMode::off : BinauralMode::x2));
    setPlain (processor, layer, pid::binauralDetune, category == 0 || category == 5 ? 9.0f : 6.0f);
    setPlain (processor, layer, pid::voiceMode, toInt (VoiceMode::poly1));
    setPlain (processor, layer, pid::vcaEnvMode, toInt (VcaEnvMode::env2));
    setPlain (processor, layer, pid::vcaEnv, 1.0f);
    setPlain (processor, layer, pid::velSens, variation % 3 == 0 ? 0.6f : 0.35f);
    setPlain (processor, layer, pid::dds1Range, toInt (Dds1Range::ft8));
    setPlain (processor, layer, pid::dds2Range, toInt (Dds2Range::ft8));
    setPlain (processor, layer, pid::dds2Mode, toInt (Dds2Mode::normOrSubOff));
    setPlain (processor, layer, pid::mix, 0.18f + 0.38f * v);
    setPlain (processor, layer, pid::lpf, 800.0f + 9000.0f * v);
    setPlain (processor, layer, pid::hpf, 10.0f);
    setPlain (processor, layer, pid::res, 0.06f + 0.18f * v);
    setPlain (processor, layer, pid::env2Sustain, 0.72f + 0.25f * v);
    setPlain (processor, layer, pid::env2Release, 0.25f + 0.75f * v);
    setPlain (processor, layer, pid::bendDds, 2.0f + 2.0f * (variation % 4));
    setPlain (processor, layer, pid::chorus, variation % 4 == 0 ? toInt (ChorusMode::oneTwo)
                                                                   : toInt (ChorusMode::one));
    setPlain (processor, layer, pid::chorusRate, variation % 4 == 0 ? 0.9f : 0.5f + 0.3f * v);
    setPlain (processor, layer, pid::delayMix, 0.03f + 0.09f * v);
    setPlain (processor, layer, pid::delayFeedback, 0.22f + 0.25f * v);
    setPlain (processor, layer, pid::delayDivision, toInt (variation % 3 == 0 ? DelayDivision::d1_8D
                                                         : variation % 3 == 1 ? DelayDivision::d1_4
                                                                              : DelayDivision::d1_8));
    setPlain (processor, layer, pid::delayPingPong, variation % 2 == 0 ? 1.0f : 0.0f);
    setPlain (processor, layer, pid::reverbMix, 0.08f + 0.1f * v);
    setPlain (processor, layer, pid::reverbSize, 0.45f + 0.3f * v);
    setPlain (processor, layer, pid::reverbDecay, 1.4f + 1.6f * v);

    switch (category)
    {
        case 0: // Pads
            setPlain (processor, layer, pid::dds1Wave, variation % 3 == 0 ? toInt (Dds1Wave::triangle) : toInt (Dds1Wave::saw));
            setPlain (processor, layer, pid::dds2Wave, variation % 2 == 0 ? toInt (Dds2Wave::sine) : toInt (Dds2Wave::triangle));
            setPlain (processor, layer, pid::env2Attack, varied (variation, 0.65f, 0.52f, 0.18f, 1.4f));
            setPlain (processor, layer, pid::env2Release, varied (variation, 4.2f, 1.7f, 2.3f, 6.0f));
            setPlain (processor, layer, pid::lfo2Vca, 0.04f + 0.12f * v);
            setPlain (processor, layer, pid::lfo2Vcf, 0.03f + 0.1f * v);
            setPlain (processor, layer, pid::delayMix, 0.1f + 0.11f * v);
            setPlain (processor, layer, pid::reverbMix, 0.25f + 0.15f * v);
            setPlain (processor, layer, pid::reverbSize, 0.75f + 0.2f * v);
            setPlain (processor, layer, pid::reverbDecay, 4.0f + 4.0f * v);
            setPlain (processor, layer, pid::reverbPreDelay, 30.0f);
            break;

        case 1: // Leads
            setPlain (processor, layer, pid::dds1Wave, variation % 4 == 0 ? toInt (Dds1Wave::square) : toInt (Dds1Wave::saw));
            setPlain (processor, layer, pid::dds2Wave, toInt (Dds2Wave::saw));
            setPlain (processor, layer, pid::env2Attack, 0.003f + 0.04f * v);
            setPlain (processor, layer, pid::env2Release, 0.15f + 0.34f * v);
            setPlain (processor, layer, pid::lpf, 2200.0f + 9000.0f * v);
            setPlain (processor, layer, pid::portamento, variation % 3 == 0 ? 0.12f : 0.0f);
            setPlain (processor, layer, pid::unison, variation % 4 == 0 ? 1.0f : 0.0f);
            setPlain (processor, layer, pid::crossMod, 0.03f + 0.15f * v);
            break;

        case 2: // Bass
            setPlain (processor, layer, pid::voiceMode, toInt (VoiceMode::solo));
            setPlain (processor, layer, pid::dds1Wave, variation % 4 == 0 ? toInt (Dds1Wave::square) : toInt (Dds1Wave::saw));
            setPlain (processor, layer, pid::dds2Range, toInt (Dds2Range::lfo));
            setPlain (processor, layer, pid::dds2Mode, variation % 2 == 0 ? toInt (Dds2Mode::ringOrSubSquare)
                                                                          : toInt (Dds2Mode::syncOrSubSine));
            setPlain (processor, layer, pid::mix, 0.08f + 0.17f * v);
            setPlain (processor, layer, pid::env2Attack, 0.001f);
            setPlain (processor, layer, pid::env2Release, 0.12f + 0.28f * v);
            setPlain (processor, layer, pid::lpf, 180.0f + 1100.0f * v);
            setPlain (processor, layer, pid::vcfEnv, 0.18f + 0.4f * v);
            setPlain (processor, layer, pid::portamento, variation % 3 == 0 ? 0.1f : 0.0f);
            setPlain (processor, layer, pid::chorus, toInt (ChorusMode::off));
            setPlain (processor, layer, pid::delayMix, 0.0f);
            setPlain (processor, layer, pid::reverbMix, 0.0f);
            break;

        case 3: // Keys
            setPlain (processor, layer, pid::dds1Wave, variation % 3 == 0 ? toInt (Dds1Wave::sine) : toInt (Dds1Wave::triangle));
            setPlain (processor, layer, pid::dds2Wave, variation % 2 == 0 ? toInt (Dds2Wave::sine) : toInt (Dds2Wave::square));
            setPlain (processor, layer, pid::env2Attack, 0.002f + 0.025f * v);
            setPlain (processor, layer, pid::env2Release, 0.3f + 0.55f * v);
            setPlain (processor, layer, pid::vcfEnv, 0.05f + 0.2f * v);
            setPlain (processor, layer, pid::mix, 0.28f + 0.3f * v);
            setPlain (processor, layer, pid::chorus, variation % 2 == 0 ? toInt (ChorusMode::off) : toInt (ChorusMode::two));
            setPlain (processor, layer, pid::velSens, 0.7f);
            break;

        case 4: // Plucks
            setPlain (processor, layer, pid::dds1Wave, variation % 2 == 0 ? toInt (Dds1Wave::triangle) : toInt (Dds1Wave::saw));
            setPlain (processor, layer, pid::dds2Wave, toInt (Dds2Wave::square));
            setPlain (processor, layer, pid::env2Attack, 0.001f);
            setPlain (processor, layer, pid::env2Decay, varied (variation, 0.58f, 0.43f, 0.12f, 1.0f));
            setPlain (processor, layer, pid::env2Sustain, 0.1f + 0.22f * v);
            setPlain (processor, layer, pid::env2Release, 0.18f + 0.42f * v);
            setPlain (processor, layer, pid::vcfEnv, 0.2f + 0.48f * v);
            setPlain (processor, layer, pid::delayMix, 0.08f + 0.12f * v);
            setPlain (processor, layer, pid::delayDivision, toInt (DelayDivision::d1_8D));
            setPlain (processor, layer, pid::velSens, 0.6f);
            break;

        case 5: // Brass and strings
            setPlain (processor, layer, pid::dds1Wave, toInt (Dds1Wave::saw));
            setPlain (processor, layer, pid::dds2Wave, variation % 3 == 0 ? toInt (Dds2Wave::square) : toInt (Dds2Wave::saw));
            setPlain (processor, layer, pid::dds2Tune, (variation % 5 - 2) * 0.08f);
            setPlain (processor, layer, pid::env2Attack, varied (variation, 0.22f, 0.19f, 0.025f, 0.5f));
            setPlain (processor, layer, pid::env2Release, 0.8f + 1.3f * v);
            setPlain (processor, layer, pid::vcfEnv, 0.12f + 0.25f * v);
            setPlain (processor, layer, pid::vcfDrive, variation % 5 == 0 ? toInt (Drive::one) : toInt (Drive::off));
            setPlain (processor, layer, pid::lfo1LrPhase, 0.2f + 0.55f * v);
            break;

        case 6: // FX
            setPlain (processor, layer, pid::dds1Wave, variation % 3 == 0 ? toInt (Dds1Wave::noise) : toInt (Dds1Wave::triangle));
            setPlain (processor, layer, pid::dds2Wave, variation % 2 == 0 ? toInt (Dds2Wave::noise) : toInt (Dds2Wave::pulse));
            setPlain (processor, layer, pid::dds2Tune, -5.0f + 10.0f * v);
            setPlain (processor, layer, pid::crossMod, 0.18f + 0.58f * v);
            setPlain (processor, layer, pid::lpf, 400.0f + 12500.0f * v);
            setPlain (processor, layer, pid::res, 0.2f + 0.52f * v);
            setPlain (processor, layer, pid::env2Attack, 0.01f + 0.25f * v);
            setPlain (processor, layer, pid::env2Release, 0.4f + 1.8f * v);
            setPlain (processor, layer, pid::delayMix, 0.2f + 0.2f * v);
            setPlain (processor, layer, pid::reverbMix, 0.2f + 0.2f * v);
            setPlain (processor, layer, pid::reverbDecay, 3.0f + 6.0f * v);
            setPlain (processor, layer, pid::lfo2Wave, toInt (Lfo2Wave::sampleHold));
            setPlain (processor, layer, pid::lfo2Dds, 0.05f + 0.35f * v);
            break;

    }

    const auto source = (ModSource) (variation % (int) ModSource::count);
    processor.getEngine().modMatrix (layer).setFixedAmount (
        source, ModTarget::lpfResonance, category == 0 || category == 5 ? 0.12f : 0.0f);
}

bool writePatch (NSynthAudioProcessor& processor, Layer layer, const juce::File& file, juce::String& error)
{
    juce::ValueTree tree ("NSYNTH_PATCH");
    tree.setProperty ("layer", toInt (layer), nullptr);
    tree.setProperty ("stateVersion", 4, nullptr);
    tree.setProperty ("factoryContent", true, nullptr);
    tree.addChild (processor.getEngine().modMatrix (layer).toValueTree (toInt (layer)), -1, nullptr);
    for (const auto& spec : getLayerParamSpecs())
    {
        auto* parameter = processor.getState().getParameter (layerParamId (layer, spec.suffix));
        if (parameter == nullptr)
        {
            error = "Could not find factory patch parameter: " + juce::String (spec.suffix);
            return false;
        }
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", layerParamId (layer, spec.suffix), nullptr);
        child.setProperty ("value", parameter->getValue(), nullptr);
        tree.addChild (child, -1, nullptr);
    }

    auto xml = tree.createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
    {
        error = "Could not write factory patch: " + file.getFullPathName();
        return false;
    }
    return true;
}

void configurePerformance (NSynthAudioProcessor& processor, int index)
{
    processor.initLayer (Layer::upper);
    processor.initLayer (Layer::lower);
    for (const auto& spec : getPerformanceParamSpecs())
        if (auto* parameter = processor.getState().getParameter (perfParamId (spec.suffix)))
            parameter->setValueNotifyingHost (parameter->getDefaultValue());

    const int upperCategory = index % (int) categories.size();
    const int lowerCategory = (index + 3) % (int) categories.size();
    configureLayer (processor, Layer::upper, upperCategory, index);
    configureLayer (processor, Layer::lower, lowerCategory, 15 - index);

    const bool split = index >= 12;
    setPerformance (processor, pid::kbdMode, split ? toInt (KeyboardMode::split) : toInt (KeyboardMode::dual));
    setPerformance (processor, pid::splitPoint, 48.0f + (float) (index % 5) * 3.0f);
    setPerformance (processor, pid::lowerDetune, (float) (index % 5 - 2) * 0.12f);
    setPerformance (processor, pid::perfDetune, (float) (index % 7 - 3) * 0.08f);
    setPerformance (processor, pid::tempo, 88.0f + (float) ((index * 7) % 65));
}

juce::String fileStem (int index, const char* name)
{
    return juce::String (index + 1).paddedLeft ('0', 2) + "_" + juce::String (name).replaceCharacter (' ', '_');
}

juce::String performanceName (int index)
{
    static constexpr const char* names[] = {
        "Aurora Assembly", "Brass Orchard", "Carbon Bloom", "Daylight Circuit",
        "Echoes in Glass", "Folding Horizon", "Gravity Garden", "Hollow Geometry",
        "Ivory Current", "Jade Machinery", "Kindred Motion", "Luminous Engine",
        "Marble Weather", "Northbound Signal", "Open Waterline", "Prism Ritual"
    };
    return juce::String (index + 1).paddedLeft ('0', 2) + "_" + juce::String (names[index]).replaceCharacter (' ', '_');
}

bool generateFactoryContent (const juce::File& root)
{
    NSynthAudioProcessor processor;
    juce::String error;
    int patchCount = 0;
    for (int category = 0; category < (int) categories.size(); ++category)
    {
        auto directory = root.getChildFile ("Patches").getChildFile (categories[(size_t) category].directory);
        if (! directory.createDirectory())
        {
            std::cerr << "Could not create " << directory.getFullPathName() << std::endl;
            return false;
        }

        for (int index = 0; index < 16; ++index)
        {
            configureLayer (processor, Layer::upper, category, index);
            const auto file = directory.getChildFile (fileStem (index, categories[(size_t) category].names[(size_t) index]))
                                      .withFileExtension ("nsynthpatch");
            if (! writePatch (processor, Layer::upper, file, error))
            {
                std::cerr << error << std::endl;
                return false;
            }
            ++patchCount;
        }
    }

    auto performanceDirectory = root.getChildFile ("Performances");
    if (! performanceDirectory.createDirectory())
    {
        std::cerr << "Could not create " << performanceDirectory.getFullPathName() << std::endl;
        return false;
    }
    for (int index = 0; index < 16; ++index)
    {
        configurePerformance (processor, index);
        const auto file = performanceDirectory.getChildFile (performanceName (index)).withFileExtension ("nsynthperf");
        if (! processor.savePerformance (file, &error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }

    std::cout << "Generated " << patchCount << " original factory patches and 16 performances in "
              << root.getFullPathName() << std::endl;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 3 || juce::String (argv[1]) != "--output")
    {
        std::cerr << "Usage: nSynthFactoryGenerator --output <Presets directory>" << std::endl;
        return 2;
    }

    const auto output = juce::File::isAbsolutePath (argv[2])
                      ? juce::File (argv[2])
                      : juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    return generateFactoryContent (output) ? 0 : 1;
}
