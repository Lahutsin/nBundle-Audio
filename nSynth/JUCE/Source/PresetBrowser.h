#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"

// Overlay preset browser: patches (loaded into UPPER or LOWER) and performances, with categories,
// search and favourites. A click auditions a preset immediately, a double click or Enter also closes.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::TextEditor::Listener,
                      private juce::KeyListener
{
public:
    explicit PresetBrowser (NSynthAudioProcessor& processor);
    ~PresetBrowser() override;

    void open (nSynth::PresetKind kind, nsynth::Layer layer);
    void close();
    bool isOpen() const noexcept { return isVisible(); }

    nSynth::PresetKind getKind() const noexcept { return kind; }
    void setKind (nSynth::PresetKind newKind);
    nsynth::Layer getTargetLayer() const noexcept { return targetLayer; }
    void setTargetLayer (nsynth::Layer layer);

    // Loads the previous / next preset of the current list (also used while the browser is closed).
    void step (int delta);
    // Rescans the user folders and rebuilds the lists.
    void refresh();

    // Called when the browser changes the target layer, so the editor can follow.
    std::function<void (nsynth::Layer)> onTargetLayerChanged;
    std::function<void()> onClosed;
    std::function<void()> onPresetLoaded;
    std::function<void (nSynth::PresetKind)> onSaveRequested;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Exposed for tests.
    int getNumVisiblePresets() const noexcept { return (int) visible.size(); }
    const nSynth::PresetEntry* getVisiblePreset (int row) const;
    juce::String getCategory() const { return category; }
    void setCategory (const juce::String& newCategory);
    void setSearchText (const juce::String& text);
    bool loadRow (int row, bool closeAfterwards);
    juce::String getStatusText() const { return status.getText(); }

private:
    class CategoryList;

    // ListBoxModel (preset list)
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void selectedRowsChanged (int row) override;
    juce::String getTooltipForRow (int row) override;

    // Arrow keys typed into the search box move through the list.
    bool keyPressed (const juce::KeyPress&, juce::Component* origin) override;

    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;

    juce::Rectangle<int> getPanelBounds() const;
    void rebuild (bool keepScrollPosition = false);
    void syncSelectionToCurrent (bool scrollToIt);
    juce::String currentKey() const;
    bool loadEntry (const nSynth::PresetEntry& entry);
    void updateButtons();
    void importFile();
    juce::Colour accent() const;

    NSynthAudioProcessor& synth;
    nSynth::PresetKind kind { nSynth::PresetKind::patch };
    nsynth::Layer targetLayer { nsynth::Layer::upper };
    juce::String category;
    std::vector<int> visible;
    bool syncing { false };

    juce::TextButton patchesTab { "PATCHES" }, performancesTab { "PERFORMANCES" };
    juce::TextButton upperTarget { "UPPER" }, lowerTarget { "LOWER" };
    juce::Label targetCaption;
    juce::TextEditor search;
    juce::TextButton closeButton { "CLOSE" };
    std::unique_ptr<CategoryList> categories;
    juce::ListBox list { "Presets" };
    juce::Label status;
    juce::TextButton initButton { "INIT LAYER" }, saveButton { "SAVE AS..." }, importButton { "IMPORT..." },
                     folderButton { "OPEN FOLDER" }, refreshButton { "RESCAN" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
