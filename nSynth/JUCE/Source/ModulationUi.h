#pragma once

#include "EditorComponents.h"
#include "Core/Engine/SynthEngine.h"

// Builds a configured control (MIDI learn, modulation) for a layer parameter.
using ControlFactory = std::function<std::unique_ptr<ParamControl> (nsynth::Layer, const char* suffix)>;
// Registers two controls sharing a cell: `whenOn` is shown while the selector parameter is on.
using SwitchedCellRegistrar = std::function<void (ParamControl* whenOff, ParamControl* whenOn, const char* selectorSuffix)>;

// Drawing surface of one MSEG: click to add a point, drag points, drag the round handle in the
// middle of a segment up/down to bend it, double-click removes a point or straightens a segment.
class MsegEditor : public juce::Component
{
public:
    MsegEditor (nsynth::SynthEngine& engine, nsynth::Layer layer, int index, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void resized() override { shapeCacheValid = false; }
    void setSnap (bool shouldSnap) { snap = shouldSnap; }
    bool getSnap() const noexcept { return snap; }
    // Called by the page timer: redraws everything when the shape changed, only the play
    // position's strip when the voice moved on.
    void refresh();

    static constexpr int kGridX = 16, kGridY = 8;

private:
    juce::Rectangle<float> plotArea() const noexcept;
    juce::Point<float> toScreen (float x, float y) const noexcept;
    juce::Point<float> fromScreen (juce::Point<float> p) const noexcept;
    juce::Point<float> handlePosition (int segmentEnd) const noexcept;   // segment ending at point `segmentEnd`
    // Grid snapping of a plot position (SNAP on, or off while Shift is held).
    juce::Point<float> snapped (juce::Point<float> v, const juce::ModifierKeys& mods) const noexcept;
    int pointAt (juce::Point<float> p) const noexcept;
    int handleAt (juce::Point<float> p) const noexcept;
    void store();
    void rebuildShapeCache();
    juce::Rectangle<int> playheadArea (float phase) const noexcept;

    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    int index;
    juce::Colour accent;
    nsynth::MsegShape shape;
    bool snap { true };
    int dragPoint { -1 }, dragHandle { -1 }, hoverPoint { -1 }, hoverHandle { -1 };
    float dragStartCurve { 0.0f };
    float lastPhase { -2.0f };
    // The shape's fill and stroke outline, rebuilt on edit, shape change and resize.
    juce::Path shapeFill, shapeStroke;
    bool shapeCacheValid { false };
};

// One MSEG: its timing controls, the drawing surface, and in the header where it is routed.
class MsegPanel : public juce::Component
{
public:
    MsegPanel (nsynth::SynthEngine& engine, nsynth::Layer layer, int index, juce::Colour accent,
               const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched);
    void paint (juce::Graphics&) override;
    void resized() override;
    // Called by the page timer while the panel is shown.
    void refresh (const nsynth::ModMatrixData& data);
    MsegEditor& getEditor() noexcept { return editor; }
    const juce::String& getRouteSummary() const noexcept { return routeSummary; }

private:
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    int index;
    juce::Colour accent;
    juce::String routeSummary;                             // "-> LPF Cutoff, Reverb Mix"
    juce::Rectangle<int> summaryArea;
    std::vector<std::unique_ptr<ParamControl>> controls;   // mode, sync, time, length
    MsegEditor editor;
    ModSourceChip chip;
    juce::TextButton shapeButton { "SHAPE" }, snapButton { "SNAP" };
};

// One modulation slot: source -> destination, amount, curve, polarity, remove.
class ModSlotRow : public juce::Component
{
public:
    ModSlotRow (nsynth::ModMatrix& matrix, int slotIndex, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setSlotIndex (int newIndex);
    int getSlotIndex() const noexcept { return slotIndex; }
    // Shows the slot's current content unless the user is editing this row. The menus list the
    // patch's MSEGs only (rebuilt when their number changes).
    void refresh (const juce::StringArray& macroNames, int msegCount);

    static constexpr int kHeight = 34;

private:
    void commit();
    nsynth::ModMatrix& matrix;
    int slotIndex;
    juce::Colour accent;
    nsynth::ModSlot shown;
    int shownMsegCount { -1 };
    bool updating { false };
    juce::ComboBox sourceBox, destinationBox;
    juce::Slider amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider curve { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton polarity { "UNI" }, removeButton { "X" };
};

class ModSlotList : public juce::Component,
                    public juce::DragAndDropTarget
{
public:
    explicit ModSlotList (nsynth::ModMatrix& matrix, juce::Colour accent);
    void refresh (const juce::StringArray& macroNames, int msegCount);
    void resized() override;
    void paint (juce::Graphics&) override;
    int getUsedRows() const noexcept { return (int) rows.size(); }
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    // A new slot with this source and no destination yet (the destination box opens next).
    void addSlotWithSource (nsynth::ModSource source);

private:
    nsynth::ModMatrix& matrix;
    juce::Colour accent;
    std::vector<std::unique_ptr<ModSlotRow>> rows;
};

// Tabs of the MSEGs a patch has: a click shows one, its x deletes it, + adds one (up to
// kMaxMsegs). Each tab shows how many active slots its MSEG drives.
class MsegTabBar : public juce::Component,
                   public juce::TooltipClient
{
public:
    MsegTabBar();
    void setState (int count, int selected, const std::array<int, nsynth::kMaxMsegs>& routes);
    void paint (juce::Graphics&) override;
    void resized() override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    std::function<void (int)> onSelect, onRemove;
    std::function<void()> onAdd;

    static constexpr int kHeight = 28;

private:
    struct Tab { juce::Rectangle<int> bounds, close; };
    std::vector<Tab> tabs() const;
    juce::Rectangle<int> addButtonBounds() const;

    int count { 0 }, selected { 0 }, hoverTab { -1 };
    bool hoverClose { false }, hoverAdd { false };
    std::array<int, nsynth::kMaxMsegs> routes {};
};

// MOD page of one layer: the MSEGs as tabs on the left, the modulation slots and source palette
// on the right.
class ModPage : public juce::Component,
                private juce::Timer
{
public:
    ModPage (juce::AudioProcessorValueTreeState& state, nsynth::SynthEngine& engine, nsynth::Layer layer, juce::Colour accent,
             const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    MsegPanel& getMsegPanel (int index) noexcept { return *msegs[(size_t) index]; }
    ModSlotList& getSlotList() noexcept { return slotList; }
    MsegTabBar& getMsegTabs() noexcept { return tabs; }
    int getSelectedMseg() const noexcept { return selectedMseg; }
    void selectMseg (int index);
    // Add / delete an MSEG of the layer (the MSEG parameters move with the shapes and routes).
    int addMseg();
    void removeMseg (int index);

private:
    void timerCallback() override;
    void layoutPalette();
    void copyMsegParameters (int from, int to);
    void resetMsegParameters (int index);

    juce::AudioProcessorValueTreeState& state;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    juce::Colour accent;
    std::array<std::unique_ptr<MsegPanel>, nsynth::kMaxMsegs> msegs;
    MsegTabBar tabs;
    int selectedMseg { 0 }, shownMsegCount { -1 };
    std::vector<std::unique_ptr<ModSourceChip>> chips;
    ModSlotList slotList;
    juce::Viewport viewport;
    juce::TextButton addButton { "+ ADD SLOT" }, clearButton { "CLEAR ALL" };
    juce::Label countLabel;
    juce::Rectangle<int> matrixFrame, paletteArea, listHeader;
};
