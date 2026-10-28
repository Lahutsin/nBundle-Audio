#include "PresetLibrary.h"
#include "NSynthPresetData.h"

namespace nSynth
{
namespace
{
const juce::StringArray& factoryCategoryOrder()
{
    static const juce::StringArray order { "Pads", "Leads", "Bass", "Keys", "Plucks", "Brass & Strings", "FX",
                                           "Layers", "Splits" };
    return order;
}

int categoryRank (const juce::String& category)
{
    const int index = factoryCategoryOrder().indexOf (category);
    return index >= 0 ? index : factoryCategoryOrder().size();
}

bool matchesSearch (const PresetEntry& entry, const juce::StringArray& words)
{
    for (const auto& word : words)
        if (! entry.name.containsIgnoreCase (word) && ! entry.category.containsIgnoreCase (word))
            return false;
    return true;
}
} // namespace

juce::String PresetEntry::key() const
{
    return juce::String (factory ? "factory" : "user") + "/" + (kind == PresetKind::patch ? "patch" : "perf") + "/"
         + (factory ? category + "/" + name : file.getFullPathName());
}

std::unique_ptr<juce::XmlElement> PresetEntry::readXml (juce::String* error) const
{
    std::unique_ptr<juce::XmlElement> xml;
    if (factory)
    {
        if (data != nullptr && dataSize > 0)
            xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, dataSize));
    }
    else if (file.existsAsFile())
    {
        xml = juce::XmlDocument::parse (file);
    }
    else if (error != nullptr)
    {
        *error = "File not found: " + file.getFullPathName();
        return {};
    }

    if (xml == nullptr && error != nullptr)
        *error = "Could not parse the preset \"" + name + "\".";
    return xml;
}

//==============================================================================
PresetLibrary::PresetLibrary (juce::File userRootDirectory) : userRoot (std::move (userRootDirectory)) {}

juce::File PresetLibrary::getUserDirectory (PresetKind kind) const
{
    return userRoot.getChildFile (kind == PresetKind::patch ? "Patches" : "Performances");
}

juce::String PresetLibrary::displayNameFromFileName (const juce::String& fileStem)
{
    auto name = fileStem;
    // Drop a leading "07_" sort prefix.
    const int underscore = name.indexOfChar ('_');
    if (underscore > 0 && underscore <= 3 && name.substring (0, underscore).containsOnly ("0123456789"))
        name = name.substring (underscore + 1);
    return name.replaceCharacter ('_', ' ').trim();
}

void PresetLibrary::rescan()
{
    scanned = true;
    entries.clear();
    addFactoryEntries();
    addUserEntries (PresetKind::patch);
    addUserEntries (PresetKind::performance);
    loadFavourites();

    std::stable_sort (entries.begin(), entries.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        if (a.kind != b.kind)       return a.kind == PresetKind::patch;
        if (a.factory != b.factory) return a.factory;
        if (a.factory)
        {
            const int ra = categoryRank (a.category), rb = categoryRank (b.category);
            if (ra != rb)                return ra < rb;
            if (a.category != b.category) return a.category.compareIgnoreCase (b.category) < 0;
            if (a.order != b.order)      return a.order < b.order;
        }
        else if (a.category != b.category)
        {
            return a.category.compareIgnoreCase (b.category) < 0;
        }
        return a.name.compareNatural (b.name) < 0;
    });
}

void PresetLibrary::addFactoryEntries()
{
    for (int i = 0; i < NSynthPresetData::namedResourceListSize; ++i)
    {
        const juce::String fileName (NSynthPresetData::originalFilenames[i]);
        const bool isPatch = fileName.endsWithIgnoreCase (patchExtension);
        if (! isPatch && ! fileName.endsWithIgnoreCase (performanceExtension))
            continue;

        int size = 0;
        const char* data = NSynthPresetData::getNamedResource (NSynthPresetData::namedResourceList[i], size);
        if (data == nullptr || size <= 0)
            continue;

        // Only the root element is needed for the index.
        juce::XmlDocument document (juce::String::fromUTF8 (data, size));
        const auto root = document.getDocumentElement (true);
        if (root == nullptr)
            continue;

        PresetEntry entry;
        entry.kind = isPatch ? PresetKind::patch : PresetKind::performance;
        entry.factory = true;
        entry.data = data;
        entry.dataSize = size;
        const auto stem = fileName.upToLastOccurrenceOf (".", false, false);
        entry.name = root->getStringAttribute ("name", displayNameFromFileName (stem));
        entry.category = root->getStringAttribute ("category", isPatch ? "Patches" : "Performances");
        entry.order = stem.initialSectionContainingOnly ("0123456789").getIntValue();
        entries.push_back (std::move (entry));
    }
}

void PresetLibrary::addUserEntries (PresetKind kind)
{
    const auto directory = getUserDirectory (kind);
    if (! directory.isDirectory())
        return;

    const auto extension = juce::String ("*") + (kind == PresetKind::patch ? patchExtension : performanceExtension);
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, true, extension))
    {
        PresetEntry entry;
        entry.kind = kind;
        entry.factory = false;
        entry.file = file;
        entry.name = displayNameFromFileName (file.getFileNameWithoutExtension());
        // Sub-folders of the user directory act as user categories.
        const auto parent = file.getParentDirectory();
        entry.category = parent == directory ? juce::String (userCategory) : parent.getFileName();
        entries.push_back (std::move (entry));
    }
}

juce::StringArray PresetLibrary::getCategories (PresetKind kind)
{
    ensureScanned();
    juce::StringArray factory, user;
    for (const auto& e : entries)
        if (e.kind == kind)
            (e.factory ? factory : user).addIfNotAlreadyThere (e.category);
    for (const auto& c : user)
        if (c != userCategory)
            factory.addIfNotAlreadyThere (c);
    return factory;
}

std::vector<int> PresetLibrary::filter (PresetKind kind, const juce::String& category, const juce::String& search)
{
    ensureScanned();
    juce::StringArray words;
    words.addTokens (search.trim(), " ", "\"");
    words.removeEmptyStrings();

    std::vector<int> result;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (e.kind != kind)
            continue;
        if (category == favouritesCategory && ! isFavourite (e))
            continue;
        if (category == userCategory && e.factory)
            continue;
        if (category.isNotEmpty() && category != favouritesCategory && category != userCategory && e.category != category)
            continue;
        if (! matchesSearch (e, words))
            continue;
        result.push_back (i);
    }
    return result;
}

int PresetLibrary::count (PresetKind kind, const juce::String& category)
{
    return (int) filter (kind, category, {}).size();
}

const PresetEntry* PresetLibrary::findByKey (const juce::String& key)
{
    ensureScanned();
    for (const auto& e : entries)
        if (e.key() == key)
            return &e;
    return nullptr;
}

void PresetLibrary::setFavourite (const PresetEntry& entry, bool shouldBeFavourite)
{
    if (shouldBeFavourite)
        favourites.addIfNotAlreadyThere (entry.key());
    else
        favourites.removeString (entry.key());
    saveFavourites();
}

void PresetLibrary::loadFavourites()
{
    favourites.clear();
    if (auto xml = juce::XmlDocument::parse (userRoot.getChildFile ("Favourites.xml")))
        for (auto* item : xml->getChildWithTagNameIterator ("ITEM"))
            favourites.addIfNotAlreadyThere (item->getStringAttribute ("key"));
}

void PresetLibrary::saveFavourites() const
{
    juce::XmlElement xml ("FAVOURITES");
    for (const auto& key : favourites)
        xml.createNewChildElement ("ITEM")->setAttribute ("key", key);
    userRoot.createDirectory();
    xml.writeTo (userRoot.getChildFile ("Favourites.xml"));
}
} // namespace nSynth
