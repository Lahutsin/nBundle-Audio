#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include "PluginProcessor.h"

class NSynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NSynthLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (9.5f)); }
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Label* createSliderTextBox (juce::Slider& s) override;

    static juce::Colour accentFor (const juce::Component&);
};

// One parameter control (rotary, fader, combo or toggle) with its caption and host attachment.
class ParamControl : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                  const nsynth::ParamSpec& spec, juce::Colour accent, const juce::String& captionOverride = {});
    void resized() override;
    const juce::String& getParamId() const noexcept { return paramId; }
    int getChoiceButtonCount() const noexcept { return (int) choiceButtons.size(); }
    void setMidiLearnHandlers (std::function<bool()> learn, std::function<void()> clear,
                               std::function<int()> assignedCC);
    void mouseDown (const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    juce::String paramId;
    juce::String captionText;
    juce::Label caption;
    std::function<bool()> learnMidiCC;
    std::function<void()> clearMidiCC;
    std::function<int()> getAssignedMidiCC;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::vector<std::unique_ptr<juce::TextButton>> choiceButtons;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

// Titled panel laying its controls out on a grid (columns x rows). A control added with
// sharesPreviousCell occupies the same cell as the one before it (only one is shown at a time).
class SectionPanel : public juce::Component
{
public:
    SectionPanel (juce::String title, juce::Colour accent, int columns);
    void add (std::unique_ptr<ParamControl> control, bool sharesPreviousCell = false);
    int getColumns() const noexcept { return columns; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    juce::Colour accent;
    int columns;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<std::vector<ParamControl*>> cells;
};

class ModMatrixGrid : public juce::Component,
                      private juce::Timer
{
public:
    ModMatrixGrid (nsynth::ModMatrix& matrix, bool direct);
    void setMatrix (nsynth::ModMatrix& matrix);
    void resized() override;

private:
    void timerCallback() override;
    float amountAt (int source, int target) const noexcept;
    bool setAmountAt (int source, int target, float amount);

    nsynth::ModMatrix* matrix;
    const bool direct;
    bool updating { false };
    std::vector<std::unique_ptr<juce::Label>> rowLabels, columnLabels;
    std::vector<std::unique_ptr<juce::Slider>> cells;
};

class ModMatrixPage : public juce::Component
{
public:
    ModMatrixPage (nsynth::SynthEngine& engine, nsynth::Layer layer);
    void setLayer (nsynth::Layer layer);
    void resized() override;
    std::function<void()> onClose;

private:
    class Tabs final : public juce::TabbedComponent
    {
    public:
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void (int)> onTabChanged;
        void currentTabChanged (int index, const juce::String&) override
        {
            if (onTabChanged != nullptr)
                onTabChanged (index);
        }
    };

    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    Tabs tabs;
    juce::TextButton closeButton { "CLOSE" }, clearAllButton { "CLEAR ALL" },
                     clearSourceButton { "CLEAR SOURCE" }, clearTargetButton { "CLEAR TARGET" };
    juce::ComboBox sourceChoice, targetChoice;
};

class WaveBrowserPage : public juce::Component,
                        private juce::Timer
{
public:
    explicit WaveBrowserPage (nsynth::SynthEngine& engine);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    void timerCallback() override;
    nsynth::SynthEngine& engine;
    juce::ComboBox slotChoice;
    juce::TextButton importButton { "IMPORT" }, exportButton { "EXPORT .WS6" }, closeButton { "CLOSE" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};

class NSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit NSynthAudioProcessorEditor (NSynthAudioProcessor&);
    ~NSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Exposed for the editor tests.
    float getScale() const noexcept { return scale; }
    juce::Component& getContent() noexcept { return content; }
    static constexpr int kDesignWidth = NSynthAudioProcessor::kDesignWidth;
    static constexpr int kDesignHeight = NSynthAudioProcessor::kDesignHeight;
    void setScale (float newScale);
    ModMatrixPage& getModMatrixPage() noexcept { return *modMatrixPage; }
    void showModMatrixPage (bool shouldShow) { modMatrixPage->setVisible (shouldShow); }
    WaveBrowserPage& getWaveBrowserPage() noexcept { return *waveBrowserPage; }
    void showWaveBrowserPage (bool shouldShow) { waveBrowserPage->setVisible (shouldShow); }
    // Cells shared by two controls (delay ms / division, static velocity / curve).
    void updateSwitchedControls();

private:
    struct LayerUi
    {
        std::vector<std::unique_ptr<SectionPanel>> rowSections;   // DDS1 .. DDS MOD (always visible)
        std::vector<std::unique_ptr<SectionPanel>> sharedSections; // perf control, voice, delay, reverb, chorus (bottom row)
        juce::Label rowLabel;
    };

    struct SwitchedCell
    {
        ParamControl* whenOff { nullptr };
        ParamControl* whenOn { nullptr };
        std::atomic<float>* condition { nullptr };
    };

    void timerCallback() override;
    void refreshDisplay() noexcept;
    std::unique_ptr<SectionPanel> makeSection (nsynth::Layer layer, nsynth::Section section, int columns, juce::Colour accent);
    void selectBottomLayer (nsynth::Layer layer);
    void configureMidiLearn (ParamControl& control);
    void layoutContent();
    void stepPreset (int delta);
    std::vector<juce::File> allPresets() const;

    NSynthAudioProcessor& synth;
    NSynthLookAndFeel lnf;
    juce::Component content;
    float scale { 1.0f };
    uint32_t lastMidiParameterDrops { 0 };

    std::array<LayerUi, 2> layers;
    std::vector<SwitchedCell> switchedCells;
    nsynth::Layer bottomLayer { nsynth::Layer::upper };

    juce::Label title, presetName, voiceMeter;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, loadPreset { "LOAD" }, savePreset { "SAVE" };
    juce::TextButton upperSelect { "UPPER" }, lowerSelect { "LOWER" };
    juce::TextButton initLayer { "INIT" }, copyUpToLow { "COPY U>L" }, copyLowToUp { "COPY L>U" };
    juce::TextButton loadPatch { "LOAD PATCH" }, savePatch { "SAVE PATCH" }, comparePatch { "COMPARE" };
    juce::TextButton modMatrixButton { "MOD MATRIX" }, waveBrowserButton { "WAVE BANK" };
    juce::ComboBox sizeBox;
    std::unique_ptr<ModMatrixPage> modMatrixPage;
    std::unique_ptr<WaveBrowserPage> waveBrowserPage;
    std::unique_ptr<SectionPanel> performancePanel, controllerPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NSynthAudioProcessorEditor)
};
