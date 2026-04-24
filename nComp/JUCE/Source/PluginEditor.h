#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class MeterNeedleComponent : public juce::Component
{
public:
    void setValue(float normalised, float fallSmoothing = 0.18f);
    void paint(juce::Graphics& g) override;

private:
    float value { 0.0f };
};

class NCompLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NCompLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
};

class NCompButton : public juce::Button
{
public:
    enum class Style
    {
        toggle,
        mode,
        power,
        state
    };

    NCompButton(const juce::String& name = {}, Style style = Style::toggle, juce::String primaryText = {}, juce::String secondaryText = {});
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    Style style;
    juce::String primaryText;
    juce::String secondaryText;
};

class NCompAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NCompAudioProcessorEditor(NCompAudioProcessor&);
    ~NCompAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void addSlider(juce::Slider& slider, const juce::String& name, const juce::String& paramId, const juce::String& suffix);
    void addCompactSlider(juce::Slider& slider, juce::Label& label, const juce::String& name, const juce::String& paramId, const juce::String& suffix);
    void addCombo(juce::ComboBox& box, const juce::String& name, const juce::String& paramId);
    void setChoiceItems(juce::ComboBox& box, const juce::String& paramId);
    void setChoiceParam(const juce::String& paramId, int index);
    void configureToolbarButton(juce::TextButton& button, bool toggledButton = false);
    void refreshPresetFileListAsync();
    void applyUserPresetFiles(std::vector<juce::File> files);
    void rebuildPresetMenu();
    void syncPresetManager();
    void handlePresetSelectionChange();
    void stepPreset(int direction);
    void openPresetFile();
    void savePreset(bool forceSaveAs);
    void showPresetMessage(const juce::String& title, const juce::String& message) const;
    int getCurrentPresetItemId() const;
    bool syncButtonsFromState();
    void setABButtonVisualState(bool slotAActive);
    std::array<juce::Slider*, 14> getKnobSliders();
    std::array<const juce::Slider*, 14> getKnobSliders() const;
    juce::Slider* findSliderForCaptionPoint(juce::Point<float> point);
    void beginKnobCaptionEdit(juce::Slider& slider);
    void endKnobCaptionEdit(bool applyValue);
    void applyKnobCaptionText(juce::Slider& slider, const juce::String& text);

    NCompAudioProcessor& processor;

    NCompLookAndFeel lnf;

    MeterNeedleComponent meterLeft;
    MeterNeedleComponent meterRight;

    juce::Slider in1Slider, in2Slider, out1Slider, out2Slider;
    juce::Slider thresh1Slider, thresh2Slider, ratio1Slider, ratio2Slider;
    juce::Slider attack1Slider, attack2Slider, release1Slider, release2Slider;
    juce::Slider mixSlider, characterSlider;
    juce::Slider linkAmountSlider, sidechainHpfSlider, sidechainTiltSlider;

    NCompButton linkButton { "Link", NCompButton::Style::toggle, "LINK" };
    NCompButton listenButton { "Sidechain Listen", NCompButton::Style::toggle, "AUD" };
    NCompButton scButton { "Sidechain", NCompButton::Style::toggle, "SC" };
    NCompButton autoGainButton { "Auto Gain", NCompButton::Style::toggle, "AUTO" };
    NCompButton mslrButton { "L/R | M/S", NCompButton::Style::mode, "L/R", "M/S" };
    NCompButton powerButton { "Power", NCompButton::Style::power, "" };
    juce::TextButton presetPrevButton { "<" };
    juce::TextButton presetNextButton { ">" };
    juce::TextButton presetOpenButton { "LOAD" };
    juce::TextButton presetSaveButton { "SAVE" };
    juce::TextButton abCopyButton { ">" };
    juce::TextButton abAButton { "A" };
    juce::TextButton abBButton { "B" };

    juce::ComboBox linkBox, mslrBox, scBox, listenBox, powerBox, autoGainBox;
    juce::ComboBox presetBox;

    juce::Label meterLabelL, meterLabelR;
    juce::Label linkAmountLabel, sidechainHpfLabel, sidechainTiltLabel;
    juce::Label transferLabel, presetLabel;
    juce::TextEditor knobValueEditor;
    juce::Slider* editingSlider { nullptr };
    int lastMslrModeIndex { -1 };
    std::vector<juce::File> userPresetFiles;
    std::atomic<uint64_t> presetScanGeneration { 0 };
    bool syncingPresetBox { false };
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NCompAudioProcessorEditor)
};
