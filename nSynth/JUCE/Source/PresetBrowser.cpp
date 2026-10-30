#include "PresetBrowser.h"
#include "UiPalette.h"
#include "UiDialogs.h"

using namespace nsynth;
using namespace nSynth::ui;
using nSynth::PresetKind;
using nSynth::PresetLibrary;

namespace
{
constexpr int kStarWidth = 30;
constexpr int kRowHeight = 28;
constexpr int kCategoryRowHeight = 30;

juce::String kindName (PresetKind kind, bool plural = true)
{
    if (kind == PresetKind::patch)
        return plural ? "patches" : "patch";
    return plural ? "performances" : "performance";
}

// A file in `directory` (or a sub-folder) with the same content as `file`.
juce::File findIdenticalFile (const juce::File& directory, const juce::File& file)
{
    if (! directory.isDirectory())
        return {};
    for (const auto& candidate : directory.findChildFiles (juce::File::findFiles, true, "*" + file.getFileExtension()))
        if (candidate.hasIdenticalContentTo (file))
            return candidate;
    return {};
}

void drawStar (juce::Graphics& g, juce::Rectangle<float> area, bool filled)
{
    juce::Path star;
    star.addStar (area.getCentre(), 5, area.getWidth() * 0.22f, area.getWidth() * 0.5f, 0.0f);
    if (filled)
    {
        g.setColour (kFavourite);
        g.fillPath (star);
    }
    else
    {
        g.setColour (kTextDim.withAlpha (0.55f));
        g.strokePath (star, juce::PathStrokeType (1.1f));
    }
}
} // namespace

//==============================================================================
// Category sidebar: All / Favourites / factory categories / User, each with its preset count.
class PresetBrowser::CategoryList final : public juce::ListBox,
                                          private juce::ListBoxModel
{
public:
    explicit CategoryList (PresetBrowser& browser) : juce::ListBox ("Categories"), owner (browser)
    {
        setModel (this);
        setRowHeight (kCategoryRowHeight);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setWantsKeyboardFocus (false);
    }

    void rebuild()
    {
        auto& library = owner.synth.getPresetLibrary();
        items.clear();
        items.push_back ({ {}, "All " + juce::String (owner.kind == PresetKind::patch ? "Patches" : "Performances") });
        items.push_back ({ PresetLibrary::favouritesCategory, PresetLibrary::favouritesCategory });
        for (const auto& c : library.getCategories (owner.kind))
            items.push_back ({ c, c });
        items.push_back ({ PresetLibrary::userCategory, PresetLibrary::userCategory });
        counts.clear();
        for (const auto& item : items)
            counts.push_back (library.count (owner.kind, item.id));
        updateContent();
        repaint();
    }

    int getNumRows() override { return (int) items.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;
        const auto& item = items[(size_t) row];
        const bool isCurrent = item.id == owner.category;
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2).toFloat();
        if (isCurrent)
        {
            g.setColour (owner.accent().withAlpha (0.22f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (owner.accent());
            g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
        }

        auto text = r.reduced (10.0f, 0.0f);
        if (item.id == PresetLibrary::favouritesCategory)
        {
            drawStar (g, text.removeFromLeft (14.0f).withSizeKeepingCentre (13.0f, 13.0f), true);
            text.removeFromLeft (6.0f);
        }
        const int count = counts[(size_t) row];
        g.setColour (count > 0 || isCurrent ? kText : kTextDim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)).withStyle (isCurrent ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (item.label, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (kTextDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (juce::String (count), text.toNearestInt(), juce::Justification::centredRight);

        if (row == 1 || row == (int) items.size() - 2)
        {
            g.setColour (kOutline);
            g.drawHorizontalLine (height - 1, 8.0f, (float) width - 8.0f);
        }
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, (int) items.size()))
            owner.setCategory (items[(size_t) row].id);
    }

private:
    struct Item { juce::String id, label; };
    PresetBrowser& owner;
    std::vector<Item> items;
    std::vector<int> counts;
};

//==============================================================================
PresetBrowser::PresetBrowser (NSynthAudioProcessor& processor) : synth (processor)
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);

    for (auto* tab : { &patchesTab, &performancesTab })
    {
        tab->setClickingTogglesState (false);
        tab->setColour (juce::TextButton::buttonOnColourId, kPerfAccent.darker (0.5f));
        addAndMakeVisible (*tab);
    }
    patchesTab.onClick = [this] { setKind (PresetKind::patch); };
    performancesTab.onClick = [this] { setKind (PresetKind::performance); };

    targetCaption.setText ("LOAD INTO", juce::dontSendNotification);
    targetCaption.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());
    targetCaption.setColour (juce::Label::textColourId, kTextDim);
    targetCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (targetCaption);
    upperTarget.setColour (juce::TextButton::buttonOnColourId, kUpperAccent.darker (0.35f));
    lowerTarget.setColour (juce::TextButton::buttonOnColourId, kLowerAccent.darker (0.35f));
    upperTarget.onClick = [this] { setTargetLayer (Layer::upper); if (onTargetLayerChanged) onTargetLayerChanged (Layer::upper); };
    lowerTarget.onClick = [this] { setTargetLayer (Layer::lower); if (onTargetLayerChanged) onTargetLayerChanged (Layer::lower); };
    addAndMakeVisible (upperTarget);
    addAndMakeVisible (lowerTarget);

    search.setTextToShowWhenEmpty ("Search name or category...", kTextDim);
    search.setFont (juce::Font (juce::FontOptions (14.0f)));
    search.setColour (juce::TextEditor::backgroundColourId, kBackground);
    search.setColour (juce::TextEditor::outlineColourId, kOutline);
    search.setColour (juce::TextEditor::focusedOutlineColourId, kPerfAccent);
    search.setColour (juce::TextEditor::textColourId, kText);
    search.setIndents (8, 6);
    search.setSelectAllWhenFocused (true);
    search.addListener (this);
    addAndMakeVisible (search);

    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    categories = std::make_unique<CategoryList> (*this);
    addAndMakeVisible (*categories);

    list.setModel (this);
    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, kBackground.darker (0.15f));
    list.setColour (juce::ListBox::outlineColourId, kOutline);
    list.setOutlineThickness (1);
    list.setWantsKeyboardFocus (true);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, kOutline.brighter (0.3f));
    addAndMakeVisible (list);

    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    status.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (status);

    initButton.onClick = [this]
    {
        const auto layer = targetLayer;
        const juce::String layerName (layer == Layer::upper ? "UPPER" : "LOWER");
        nSynth::ui::confirmThen (synth.isPatchDirty (layer), messageBox, this, "Init layer",
                                 layerName + " has unsaved changes. Reset it to the init patch anyway?", "Reset",
                                 [this, layer, layerName]
                                 {
                                     synth.initLayer (layer);
                                     status.setText (layerName + " reset to the init patch.", juce::dontSendNotification);
                                     updateCurrentRow();
                                     list.repaint();
                                 });
    };
    saveButton.onClick = [this] { if (onSaveRequested) onSaveRequested (kind); };
    importButton.onClick = [this] { importFile(); };
    folderButton.onClick = [this]
    {
        const auto dir = synth.getPresetLibrary().getUserDirectory (kind);
        dir.createDirectory();
        dir.revealToUser();
    };
    refreshButton.onClick = [this] { refresh(); };
    for (auto* b : { &initButton, &saveButton, &importButton, &folderButton, &refreshButton })
        addAndMakeVisible (*b);

    search.addKeyListener (this);
    updateButtons();
}

PresetBrowser::~PresetBrowser()
{
    search.removeKeyListener (this);
    list.setModel (nullptr);
}

juce::Colour PresetBrowser::accent() const
{
    if (kind == PresetKind::performance)
        return kPerfAccent;
    return targetLayer == Layer::upper ? kUpperAccent : kLowerAccent;
}

void PresetBrowser::open (PresetKind newKind, Layer layer)
{
    kind = newKind;
    targetLayer = layer;
    if (category.isNotEmpty() && category != PresetLibrary::favouritesCategory && category != PresetLibrary::userCategory
        && ! synth.getPresetLibrary().getCategories (kind).contains (category))
        category = {};
    synth.getPresetLibrary().ensureScanned();
    updateButtons();
    rebuild (false, true);
    setVisible (true);
    toFront (false);
    syncSelectionToCurrent (true);
    search.grabKeyboardFocus();
}

void PresetBrowser::close()
{
    setVisible (false);
}

void PresetBrowser::visibilityChanged()
{
    if (! isVisible() && onClosed != nullptr)
        onClosed();
}

void PresetBrowser::setKind (PresetKind newKind)
{
    if (kind == newKind)
        return;
    kind = newKind;
    category = {};
    updateButtons();
    rebuild (false, true);
    syncSelectionToCurrent (true);
}

void PresetBrowser::setTargetLayer (Layer layer)
{
    targetLayer = layer;
    updateButtons();
    syncSelectionToCurrent (true);
    repaint();
}

void PresetBrowser::setCategory (const juce::String& newCategory)
{
    category = newCategory;
    rebuild();
    syncSelectionToCurrent (true);
}

void PresetBrowser::setSearchText (const juce::String& text)
{
    // TextEditor notifies its listeners asynchronously; rebuild right away.
    search.setText (text, false);
    rebuild();
    syncSelectionToCurrent (true);
}

void PresetBrowser::refresh()
{
    synth.getPresetLibrary().rescan();
    rebuild (true, true);
    syncSelectionToCurrent (false);
}

void PresetBrowser::updateButtons()
{
    const bool patches = kind == PresetKind::patch;
    patchesTab.setToggleState (patches, juce::dontSendNotification);
    performancesTab.setToggleState (! patches, juce::dontSendNotification);
    upperTarget.setToggleState (targetLayer == Layer::upper, juce::dontSendNotification);
    lowerTarget.setToggleState (targetLayer == Layer::lower, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &targetCaption, &upperTarget, &lowerTarget, &initButton })
        c->setVisible (patches);
    resized();
}

void PresetBrowser::rebuild (bool keepScrollPosition, bool recountCategories)
{
    const auto scroll = list.getVerticalPosition();
    visible = synth.getPresetLibrary().filter (kind, category, search.getText());
    updateCurrentRow();
    if (recountCategories)
        categories->rebuild();
    else
        categories->repaint();   // the highlighted category
    {
        const juce::ScopedValueSetter<bool> guard (syncing, true);
        list.deselectAllRows();
        list.updateContent();
    }
    if (keepScrollPosition)
        list.setVerticalPosition (scroll);
    list.repaint();

    juce::String text;
    text << (int) visible.size() << " " << kindName (kind, visible.size() != 1);
    if (search.getText().isNotEmpty())
        text << " matching \"" << search.getText().trim() << "\"";
    text << "   |   click: audition   double-click / Enter: load & close   arrows: step";
    status.setText (text, juce::dontSendNotification);
}

juce::String PresetBrowser::currentKey() const
{
    return kind == PresetKind::patch ? synth.getPatchKey (targetLayer) : synth.getPerformanceKey();
}

void PresetBrowser::updateCurrentRow()
{
    const auto key = currentKey();
    const auto& library = synth.getPresetLibrary();
    currentRow = -1;
    if (key.isNotEmpty())
        for (int row = 0; row < (int) visible.size() && currentRow < 0; ++row)
            if (library.getKey (visible[(size_t) row]) == key)
                currentRow = row;
}

void PresetBrowser::syncSelectionToCurrent (bool scrollToIt)
{
    updateCurrentRow();
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    if (currentRow < 0)
    {
        list.deselectAllRows();
        return;
    }
    list.selectRow (currentRow, ! scrollToIt, true);
    if (scrollToIt)
        list.scrollToEnsureRowIsOnscreen (currentRow);
}

const nSynth::PresetEntry* PresetBrowser::getVisiblePreset (int row) const
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return nullptr;
    return &synth.getPresetLibrary().getEntries()[(size_t) visible[(size_t) row]];
}

bool PresetBrowser::loadEntry (const nSynth::PresetEntry& entry)
{
    juce::String error;
    const bool ok = entry.kind == PresetKind::patch ? synth.loadPatch (targetLayer, entry, &error)
                                                    : synth.loadPerformance (entry, &error);
    if (ok)
    {
        juce::String text;
        text << "Loaded " << (entry.kind == PresetKind::patch ? (targetLayer == Layer::upper ? "UPPER: " : "LOWER: ") : "")
             << entry.name << "  (" << entry.category << (entry.factory ? "" : ", user") << ")";
        status.setText (text, juce::dontSendNotification);
        if (onPresetLoaded != nullptr)
            onPresetLoaded();
    }
    else
    {
        status.setText ("Could not load \"" + entry.name + "\": " + error, juce::dontSendNotification);
    }
    updateCurrentRow();
    list.repaint();
    return ok;
}

bool PresetBrowser::loadRow (int row, bool closeAfterwards)
{
    const auto* entry = getVisiblePreset (row);
    if (entry == nullptr)
        return false;
    const bool ok = loadEntry (*entry);
    if (ok && closeAfterwards)
        close();
    return ok;
}

void PresetBrowser::step (int delta)
{
    auto& library = synth.getPresetLibrary();
    library.ensureScanned();
    const auto key = currentKey();

    // Open: the shown list. Closed: the loaded preset's category (everything of the kind when it
    // did not come from the library), without the search text of the last browse.
    std::vector<int> categoryList;
    if (! isVisible())
    {
        const auto* current = library.findByKey (key);
        categoryList = library.filter (kind, current != nullptr ? current->category : juce::String(), {});
    }
    const auto& presets = isVisible() ? visible : categoryList;
    if (presets.empty())
        return;

    int index = -1;
    for (int row = 0; row < (int) presets.size(); ++row)
        if (library.getKey (presets[(size_t) row]) == key)
            index = row;

    const int count = (int) presets.size();
    const int next = index < 0 ? (delta > 0 ? 0 : count - 1) : ((index + delta) % count + count) % count;
    loadEntry (library.getEntries()[(size_t) presets[(size_t) next]]);
    if (isVisible())
        syncSelectionToCurrent (true);
}

void PresetBrowser::importFile()
{
    const auto dir = synth.getPresetLibrary().getUserDirectory (kind);
    chooser = std::make_unique<juce::FileChooser> ("Import " + kindName (kind, false), dir,
                                                   kind == PresetKind::patch ? "*.nsynthpatch" : "*.nsynthperf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, dir] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        const auto load = [this, &error] (const juce::File& f)
        {
            return kind == PresetKind::patch ? synth.loadPatch (targetLayer, f, &error)
                                             : synth.loadPerformance (f, &error);
        };
        // Loading validates the file: nothing is copied unless it loads.
        if (! load (file))
        {
            status.setText ("Import failed: " + error, juce::dontSendNotification);
            return;
        }

        // Files from elsewhere are copied into the user folder so they stay in the browser; when
        // the same file is already there, that one is used instead of a numbered duplicate.
        juce::String result ("Imported " + file.getFileName());
        if (! file.isAChildOf (dir))
        {
            auto copy = findIdenticalFile (dir, file);
            if (copy == juce::File())
            {
                copy = dir.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
                if (dir.createDirectory().failed() || ! file.copyFileTo (copy))
                {
                    result << " (loaded, but it could not be copied into " << dir.getFullPathName() << ")";
                    copy = juce::File();
                }
            }
            // The copy is what the browser lists: load it so it shows as the current preset.
            if (copy != juce::File())
                load (copy);
        }
        refresh();
        status.setText (result, juce::dontSendNotification);
    });
}

//==============================================================================
void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto* entry = getVisiblePreset (row);
    if (entry == nullptr)
        return;

    const bool current = row == currentRow;
    auto r = juce::Rectangle<int> (0, 0, width, height);
    if (row % 2 == 1)
    {
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        g.fillRect (r);
    }
    if (selected || current)
    {
        g.setColour (accent().withAlpha (selected ? 0.24f : 0.12f));
        g.fillRect (r);
    }
    if (current)
    {
        g.setColour (accent());
        g.fillRect (r.withWidth (3));
    }

    drawStar (g, r.removeFromLeft (kStarWidth).toFloat().withSizeKeepingCentre (13.0f, 13.0f),
              synth.getPresetLibrary().isFavourite (visible[(size_t) row]));

    r.removeFromRight (10);
    auto right = r.removeFromRight (juce::jmin (220, width / 3));
    if (! entry->factory)
    {
        auto badge = right.removeFromRight (42).withSizeKeepingCentre (40, 16).toFloat();
        g.setColour (kPerfAccent.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (kPerfAccent);
        g.setFont (juce::Font (juce::FontOptions (9.5f)).boldened());
        g.drawText ("USER", badge, juce::Justification::centred);
        right.removeFromRight (6);
    }
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (entry->category, right, juce::Justification::centredRight);

    g.setColour (current ? kText : kText.withAlpha (0.9f));
    g.setFont (juce::Font (juce::FontOptions (14.5f)).withStyle (current ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText (entry->name, r.reduced (4, 0), juce::Justification::centredLeft, 1, 0.8f);
}

void PresetBrowser::selectedRowsChanged (int row)
{
    if (syncing || row < 0)
        return;
    // A click on the star only toggles the favourite, it does not load the preset.
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() && list.getMouseXYRelative().x < kStarWidth)
        return;
    loadRow (row, false);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto* entry = getVisiblePreset (row);
    if (entry == nullptr)
        return;
    if (e.x < kStarWidth)
    {
        auto& library = synth.getPresetLibrary();
        if (! library.setFavourite (*entry, ! library.isFavourite (visible[(size_t) row])))
            status.setText ("Could not save the favourites in " + library.getUserDirectory (kind).getParentDirectory().getFullPathName(),
                            juce::dontSendNotification);
        if (category == PresetLibrary::favouritesCategory)
            rebuild (true, true);
        else
            categories->rebuild();
        list.repaint();
        syncSelectionToCurrent (false);
    }
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x >= kStarWidth)
        loadRow (row, true);
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (getVisiblePreset (row) != nullptr && row != list.getSelectedRow())
        loadRow (row, true);
    else
        close();
}

juce::String PresetBrowser::getTooltipForRow (int row)
{
    const auto* entry = getVisiblePreset (row);
    if (entry == nullptr)
        return {};
    return entry->name + " - " + entry->category + (entry->factory ? " (factory)" : " (user: " + entry->file.getFullPathName() + ")");
}

void PresetBrowser::textEditorTextChanged (juce::TextEditor&)
{
    rebuild();
    syncSelectionToCurrent (true);
}

void PresetBrowser::textEditorReturnKeyPressed (juce::TextEditor&)
{
    // Enter in the search box loads the first match when nothing is selected yet.
    if (list.getSelectedRow() < 0 && ! visible.empty())
        loadRow (0, true);
    else
        close();
}

void PresetBrowser::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    if (search.getText().isEmpty())
    {
        close();
        return;
    }
    search.clear();
    rebuild();
    syncSelectionToCurrent (true);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
        || key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
        return keyPressed (key);
    return false;
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
        || key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
    {
        if (visible.empty())
            return true;
        const int page = juce::jmax (1, list.getHeight() / kRowHeight - 1);
        const int delta = key == juce::KeyPress::upKey ? -1 : key == juce::KeyPress::downKey ? 1
                        : key == juce::KeyPress::pageUpKey ? -page : page;
        const int current = list.getSelectedRow();
        const int next = current < 0 ? (delta > 0 ? 0 : (int) visible.size() - 1)
                                     : juce::jlimit (0, (int) visible.size() - 1, current + delta);
        list.selectRow (next);   // selectedRowsChanged auditions it
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    return false;
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    // A click on the dimmed area around the panel closes the browser.
    if (! getPanelBounds().contains (e.getPosition()))
        close();
}

//==============================================================================
juce::Rectangle<int> PresetBrowser::getPanelBounds() const
{
    auto r = getLocalBounds();
    r.removeFromTop (44);
    return r.withSizeKeepingCentre (juce::jmin (1040, r.getWidth() - 20), r.getHeight() - 10).withY (r.getY());
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto panel = getPanelBounds().toFloat();
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 18, {}).drawForRectangle (g, panel.toNearestInt());
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (accent());
    g.fillRect (panel.withHeight (3.0f).reduced (8.0f, 0.0f));
}

void PresetBrowser::resized()
{
    auto panel = getPanelBounds().reduced (14, 12);
    panel.removeFromTop (2);

    auto top = panel.removeFromTop (32);
    patchesTab.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (4);
    performancesTab.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (16);
    closeButton.setBounds (top.removeFromRight (76));
    top.removeFromRight (16);
    if (kind == PresetKind::patch)
    {
        lowerTarget.setBounds (top.removeFromRight (74));
        top.removeFromRight (4);
        upperTarget.setBounds (top.removeFromRight (74));
        targetCaption.setBounds (top.removeFromRight (80));
        top.removeFromRight (16);
    }
    search.setBounds (top);

    panel.removeFromTop (10);
    auto footer = panel.removeFromBottom (30);
    panel.removeFromBottom (8);

    categories->setBounds (panel.removeFromLeft (210));
    panel.removeFromLeft (10);
    list.setBounds (panel);

    for (auto* b : { &refreshButton, &folderButton, &importButton, &saveButton })
    {
        b->setBounds (footer.removeFromRight (b == &folderButton ? 108 : 92));
        footer.removeFromRight (6);
    }
    if (kind == PresetKind::patch)
    {
        initButton.setBounds (footer.removeFromRight (96));
        footer.removeFromRight (6);
    }
    status.setBounds (footer);
}
