#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include "Core/Params/ParamRegistry.h"
#include "Core/Engine/ModMatrix.h"
#include "UiPalette.h"

class NSynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NSynthLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (11.5f)); }
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Label* createSliderTextBox (juce::Slider& s) override;

    static juce::Colour accentFor (const juce::Component&);
};

// What a knob needs from the editor to take part in modulation: the right-click menu entries,
// drag-and-drop of a source chip onto it, and whether its parameter can be modulated at all.
class ModulationHost
{
public:
    virtual ~ModulationHost() = default;
    virtual bool canModulate (nsynth::Layer layer, int parameterIndex) const = 0;
    // Menu item ids from kModulationMenuBase upwards belong to the host.
    virtual void addModulationMenuItems (juce::PopupMenu& menu, nsynth::Layer layer, int parameterIndex) = 0;
    virtual void handleModulationMenu (int result, nsynth::Layer layer, int parameterIndex) = 0;
    virtual void dropModulationSource (nsynth::ModSource source, nsynth::Layer layer, int parameterIndex) = 0;

    static constexpr int kModulationMenuBase = 1000;
    static juce::String dragDescription (nsynth::ModSource source) { return "modsrc:" + juce::String ((int) source); }
    // The source of a chip being dragged, or ModSource::count for anything else.
    static nsynth::ModSource sourceFromDrag (const juce::var& description);
};

// One parameter control (rotary, fader, combo or toggle) with its caption and host attachment.
// Rotary controls of modulated parameters draw a modulation ring (range and live value).
class ParamControl : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::DragAndDropTarget,
                     private juce::Timer
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                  const nsynth::ParamSpec& spec, juce::Colour accent, const juce::String& captionOverride = {});
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    const juce::String& getParamId() const noexcept { return paramId; }
    int getChoiceButtonCount() const noexcept { return (int) choiceButtons.size(); }
    juce::Slider* getSlider() noexcept { return slider.get(); }
    void setMidiLearnHandlers (std::function<bool()> learn, std::function<void()> clear,
                               std::function<int()> assignedCC);
    void mouseDown (const juce::MouseEvent& event) override;

    // Layer parameter this control edits (index in getLayerParamSpecs()); -1 for performance parameters.
    void setModulationTarget (ModulationHost* host, nsynth::Layer layer, int parameterIndex);
    nsynth::Layer getLayer() const noexcept { return layer; }
    int getParameterIndex() const noexcept { return parameterIndex; }
    // Modulation ring, in fractions of the parameter's normalised range. Repaints when it changes.
    void setModulationDisplay (bool modulated, float rangeLow, float rangeHigh, float live);

    // Only the control itself (power switches in card headers).
    void setCaptionVisible (bool shouldShow);
    // Caption edited by double-click (macro names).
    void setEditableCaption (std::function<juce::String()> getText, std::function<void (const juce::String&)> setText);

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    void timerCallback() override;
    juce::String paramId;
    juce::String captionText;
    juce::Label caption;
    bool captionVisible { true };
    std::function<bool()> learnMidiCC;
    std::function<void()> clearMidiCC;
    std::function<int()> getAssignedMidiCC;
    std::function<juce::String()> getCaptionText;
    ModulationHost* modulationHost { nullptr };
    nsynth::Layer layer { nsynth::Layer::upper };
    int parameterIndex { -1 };
    bool dragHover { false };
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::vector<std::unique_ptr<juce::TextButton>> choiceButtons;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

// A draggable modulation source: drop it on a knob (or a slot) to route it; a click calls onClick.
class ModSourceChip : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    ModSourceChip (nsynth::ModSource source, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    nsynth::ModSource getSource() const noexcept { return source; }
    void setLabel (const juce::String& text) { label = text; repaint(); }
    void setInUse (bool used) { if (used != inUse) { inUse = used; repaint(); } }
    int getPreferredWidth() const;
    std::function<void()> onClick;

private:
    nsynth::ModSource source;
    juce::Colour accent;
    juce::String label;
    bool inUse { false }, dragging { false };
};

// Titled panel laying its controls out on a grid (columns x rows). A control added with
// sharesPreviousCell occupies the same cell as the one before it (only one is shown at a time).
// Header components (source chips) sit right-aligned in the title row.
class SectionPanel : public juce::Component
{
public:
    SectionPanel (juce::String title, juce::Colour accent, int columns);
    void add (std::unique_ptr<ParamControl> control, bool sharesPreviousCell = false);
    void addHeaderComponent (std::unique_ptr<juce::Component> component);
    int getColumns() const noexcept { return columns; }
    void setColumns (int newColumns) { columns = juce::jmax (1, newColumns); resized(); }
    const std::vector<std::unique_ptr<ParamControl>>& getControls() const noexcept { return controls; }
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kTitleHeight = 17;
    // The panel look shared by every framed box of the editor.
    static void paintFrame (juce::Graphics&, juce::Rectangle<int> bounds, juce::Colour accent, const juce::String& title);

protected:
    juce::String title;
    juce::Colour accent;
    int columns;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<std::vector<ParamControl*>> cells;
    std::vector<std::unique_ptr<juce::Component>> headerComponents;
};
