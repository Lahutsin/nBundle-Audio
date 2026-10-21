#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace nSynth
{

// File discovery and XML I/O shared by factory/user Performance presets.
// Layer Patch files use the same XML representation through the processor.
class PresetManager
{
public:
    PresetManager (juce::File factoryDirectory, juce::File userDirectory, juce::String extension);

    juce::File getUserPresetDirectory() const { return userDir; }
    std::vector<juce::File> getFactoryPresets() const;
    std::vector<juce::File> getUserPresets() const;

    bool loadFromFile (juce::ValueTree& targetState, const juce::File& file, juce::String* error = nullptr) const;
    bool saveToFile (const juce::ValueTree& stateToSave, const juce::File& file, juce::String* error = nullptr) const;

private:
    std::vector<juce::File> collect (const juce::File& dir) const;

    juce::File factoryDir, userDir;
    juce::String fileExtension;
};

} // namespace nSynth
