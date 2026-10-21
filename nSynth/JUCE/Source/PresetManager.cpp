#include "PresetManager.h"

namespace nSynth
{

PresetManager::PresetManager (juce::File factoryDirectory, juce::File userDirectory, juce::String extension)
    : factoryDir (std::move (factoryDirectory)), userDir (std::move (userDirectory)), fileExtension (std::move (extension))
{
    if (! userDir.exists())
        userDir.createDirectory();
}

std::vector<juce::File> PresetManager::collect (const juce::File& dir) const
{
    std::vector<juce::File> result;
    if (! dir.exists())
        return result;

    for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*" + fileExtension))
        result.push_back (f);

    std::sort (result.begin(), result.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
    });
    return result;
}

std::vector<juce::File> PresetManager::getFactoryPresets() const { return collect (factoryDir); }
std::vector<juce::File> PresetManager::getUserPresets() const { return collect (userDir); }

bool PresetManager::loadFromFile (juce::ValueTree& targetState, const juce::File& file, juce::String* error) const
{
    if (! file.existsAsFile())
    {
        if (error) *error = "File not found: " + file.getFullPathName();
        return false;
    }

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        if (error) *error = "Could not parse preset XML.";
        return false;
    }

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
    {
        if (error) *error = "Preset file did not contain a valid state tree.";
        return false;
    }

    targetState = tree;
    return true;
}

bool PresetManager::saveToFile (const juce::ValueTree& stateToSave, const juce::File& file, juce::String* error) const
{
    auto xml = stateToSave.createXml();
    if (xml == nullptr)
    {
        if (error) *error = "Could not serialise plugin state.";
        return false;
    }

    if (! xml->writeTo (file))
    {
        if (error) *error = "Could not write file: " + file.getFullPathName();
        return false;
    }
    return true;
}

} // namespace nSynth
