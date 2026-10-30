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

class MsegPanel : public juce::Component
{
public:
    MsegPanel (nsynth::SynthEngine& engine, nsynth::Layer layer, int index, juce::Colour accent,
               const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() { editor.refresh(); }
    MsegEditor& getEditor() noexcept { return editor; }

private:
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    int index;
    juce::Colour accent;
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
    // Shows the slot's current content unless the user is editing this row.
    void refresh (const juce::StringArray& macroNames);

    static constexpr int kHeight = 34;

private:
    void commit();
    nsynth::ModMatrix& matrix;
    int slotIndex;
    juce::Colour accent;
    nsynth::ModSlot shown;
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
    void refresh (const juce::StringArray& macroNames);
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

// MOD page of one layer: MSEG 1 and 2 on the left, the modulation slots and source palette on the right.
class ModPage : public juce::Component,
                private juce::Timer
{
public:
    ModPage (nsynth::SynthEngine& engine, nsynth::Layer layer, juce::Colour accent,
             const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    MsegPanel& getMsegPanel (int index) noexcept { return *msegs[(size_t) index]; }
    ModSlotList& getSlotList() noexcept { return slotList; }

private:
    void timerCallback() override;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    juce::Colour accent;
    std::array<std::unique_ptr<MsegPanel>, 2> msegs;
    std::vector<std::unique_ptr<ModSourceChip>> chips;
    ModSlotList slotList;
    juce::Viewport viewport;
    juce::TextButton addButton { "+ ADD SLOT" }, clearButton { "CLEAR ALL" };
    juce::Label countLabel;
    juce::Rectangle<int> matrixFrame, paletteArea, listHeader;
};
