#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace nSynth
{
enum class PresetKind { patch, performance };

// One browsable preset: a factory preset embedded in the binary or a user file on disk.
struct PresetEntry
{
    PresetKind kind { PresetKind::patch };
    juce::String name, category;
    bool factory { true };
    juce::File file;                 // user presets
    const char* data { nullptr };    // factory presets (embedded XML)
    int dataSize { 0 };
    int order { 0 };                 // factory: position inside its category

    // Stable identity used for favourites and to highlight the loaded preset.
    juce::String key() const;
    std::unique_ptr<juce::XmlElement> readXml (juce::String* error = nullptr) const;
};

// Index of every factory and user patch / performance, with categories, search and favourites.
// Message thread only.
class PresetLibrary
{
public:
    static constexpr const char* favouritesCategory = "Favourites";
    static constexpr const char* userCategory = "User";
    static constexpr const char* patchExtension = ".nsynthpatch";
    static constexpr const char* performanceExtension = ".nsynthperf";

    explicit PresetLibrary (juce::File userRootDirectory);

    // Re-reads the user folders (factory content is compiled in and never changes).
    void rescan();
    void ensureScanned() { if (! scanned) rescan(); }

    const std::vector<PresetEntry>& getEntries() { ensureScanned(); return entries; }
    // getEntries()[index].key(), built once per scan.
    const juce::String& getKey (int index) const { return keys[(size_t) index]; }
    // Factory categories in their musical order, followed by user sub-folders.
    juce::StringArray getCategories (PresetKind kind);
    // Indices into getEntries(). category: empty = all, favouritesCategory, userCategory or a category name.
    std::vector<int> filter (PresetKind kind, const juce::String& category, const juce::String& search);
    const PresetEntry* findByKey (const juce::String& key);
    int count (PresetKind kind, const juce::String& category);

    bool isFavourite (const PresetEntry& entry) const { return favourites.contains (favouriteKey (entry)); }
    bool isFavourite (int index) const { return favouriteFlags[(size_t) index]; }
    // Re-reads Favourites.xml first (another plug-in instance may have changed it), applies this
    // change and writes the file atomically. Returns false when it could not be written.
    bool setFavourite (const PresetEntry& entry, bool shouldBeFavourite);
    // The key stored in Favourites.xml: user presets relative to the user folder, so the
    // favourites survive moving it.
    juce::String favouriteKey (const PresetEntry& entry) const;

    juce::File getUserDirectory (PresetKind kind) const;
    // The embedded factory presets, sorted (any thread; built once per process).
    static const std::vector<PresetEntry>& getFactoryEntries();
    // "03_Arc_Runner" -> "Arc Runner"
    static juce::String displayNameFromFileName (const juce::String& fileStem);

private:
    void addFactoryEntries();
    void addUserEntries (PresetKind kind);
    bool matches (const PresetEntry& entry, size_t index, PresetKind kind, const juce::String& category,
                  const juce::StringArray& words) const;
    void loadFavourites();
    bool saveFavourites() const;
    void updateFavouriteFlags();

    juce::File userRoot;
    std::vector<PresetEntry> entries;
    std::vector<juce::String> keys;
    std::vector<bool> favouriteFlags;
    juce::StringArray favourites;
    bool scanned { false };
};
} // namespace nSynth
