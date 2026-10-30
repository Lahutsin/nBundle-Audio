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

    keys.clear();
    keys.reserve (entries.size());
    for (const auto& e : entries)
        keys.push_back (e.key());
    updateFavouriteFlags();
}

const std::vector<PresetEntry>& PresetLibrary::getFactoryEntries()
{
    // Factory content is compiled in: it is indexed once per process, in its musical order.
    static const std::vector<PresetEntry> factory = []
    {
    std::vector<PresetEntry> entries;
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
    std::stable_sort (entries.begin(), entries.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        if (a.kind != b.kind)
            return a.kind == PresetKind::patch;
        const int ra = categoryRank (a.category), rb = categoryRank (b.category);
        if (ra != rb)
            return ra < rb;
        if (a.category != b.category)
            return a.category.compareIgnoreCase (b.category) < 0;
        if (a.order != b.order)
            return a.order < b.order;
        return a.name.compareNatural (b.name) < 0;
    });
    return entries;
    }();
    return factory;
}

void PresetLibrary::addFactoryEntries()
{
    const auto& factory = getFactoryEntries();
    entries.insert (entries.end(), factory.begin(), factory.end());
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

bool PresetLibrary::matches (const PresetEntry& e, size_t index, PresetKind kind, const juce::String& category,
                             const juce::StringArray& words) const
{
    if (e.kind != kind)
        return false;
    if (category == favouritesCategory && ! favouriteFlags[index])
        return false;
    if (category == userCategory && e.factory)
        return false;
    if (category.isNotEmpty() && category != favouritesCategory && category != userCategory && e.category != category)
        return false;
    return matchesSearch (e, words);
}

std::vector<int> PresetLibrary::filter (PresetKind kind, const juce::String& category, const juce::String& search)
{
    ensureScanned();
    juce::StringArray words;
    words.addTokens (search.trim(), " ", "\"");
    words.removeEmptyStrings();

    std::vector<int> result;
    for (size_t i = 0; i < entries.size(); ++i)
        if (matches (entries[i], i, kind, category, words))
            result.push_back ((int) i);
    return result;
}

int PresetLibrary::count (PresetKind kind, const juce::String& category)
{
    ensureScanned();
    int n = 0;
    for (size_t i = 0; i < entries.size(); ++i)
        n += matches (entries[i], i, kind, category, {}) ? 1 : 0;
    return n;
}

const PresetEntry* PresetLibrary::findByKey (const juce::String& key)
{
    ensureScanned();
    for (size_t i = 0; i < entries.size(); ++i)
        if (keys[i] == key)
            return &entries[i];
    return nullptr;
}

juce::String PresetLibrary::favouriteKey (const PresetEntry& entry) const
{
    if (entry.factory || ! entry.file.isAChildOf (userRoot))
        return entry.key();
    return juce::String ("user/") + (entry.kind == PresetKind::patch ? "patch" : "perf") + "/"
         + entry.file.getRelativePathFrom (userRoot).replaceCharacter ('\\', '/');
}

bool PresetLibrary::setFavourite (const PresetEntry& entry, bool shouldBeFavourite)
{
    // Merge with what other instances saved since our last read, then change only this entry.
    loadFavourites();
    const auto key = favouriteKey (entry);
    if (shouldBeFavourite)
        favourites.addIfNotAlreadyThere (key);
    else
        favourites.removeString (key);
    updateFavouriteFlags();
    return saveFavourites();
}

void PresetLibrary::updateFavouriteFlags()
{
    favouriteFlags.assign (entries.size(), false);
    for (size_t i = 0; i < entries.size(); ++i)
        favouriteFlags[i] = favourites.contains (favouriteKey (entries[i]));
}

void PresetLibrary::loadFavourites()
{
    favourites.clear();
    if (auto xml = juce::XmlDocument::parse (userRoot.getChildFile ("Favourites.xml")))
        for (auto* item : xml->getChildWithTagNameIterator ("ITEM"))
        {
            auto key = item->getStringAttribute ("key");
            // Older versions stored user presets by absolute path ("user/patch//Users/...").
            if (key.startsWith ("user/"))
            {
                const auto prefix = "user/" + key.substring (5).upToFirstOccurrenceOf ("/", true, false);
                const auto path = key.substring (prefix.length());
                if (juce::File::isAbsolutePath (path) && juce::File (path).isAChildOf (userRoot))
                    key = prefix + juce::File (path).getRelativePathFrom (userRoot).replaceCharacter ('\\', '/');
            }
            favourites.addIfNotAlreadyThere (key);
        }
}

bool PresetLibrary::saveFavourites() const
{
    juce::XmlElement xml ("FAVOURITES");
    for (const auto& key : favourites)
        xml.createNewChildElement ("ITEM")->setAttribute ("key", key);
    if (! userRoot.createDirectory())
        return false;
    // Written next to the target and moved over it, so a reader never sees a half-written file.
    juce::TemporaryFile temp (userRoot.getChildFile ("Favourites.xml"));
    return xml.writeTo (temp.getFile()) && temp.overwriteTargetFileWithTemporary();
}
} // namespace nSynth
