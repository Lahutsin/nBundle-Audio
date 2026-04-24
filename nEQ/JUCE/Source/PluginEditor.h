#pragma once

#include <atomic>
#include <array>
#include <vector>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class NEQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class NEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    explicit NEQAudioProcessorEditor(NEQAudioProcessor&);
    ~NEQAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct StripControls
    {
        juce::Slider hfGainSlider;
        juce::Slider hfFreqSlider;
        juce::Slider hmfGainSlider;
        juce::Slider hmfFreqSlider;
        juce::Slider hmfQSlider;
        juce::Slider lmfGainSlider;
        juce::Slider lmfFreqSlider;
        juce::Slider lmfQSlider;
        juce::Slider lfGainSlider;
        juce::Slider lfFreqSlider;

        juce::ToggleButton hfBellButton;
        juce::ToggleButton hfEnableButton;
        juce::ToggleButton blackButton;
        juce::ToggleButton hmfEnableButton;
        juce::ToggleButton lmfEnableButton;
        juce::ToggleButton lfBellButton;
        juce::ToggleButton lfEnableButton;
    };

    static void configureKnob(juce::Slider& slider, juce::Colour colour, const juce::String& suffix = {}, bool whiteStyle = false);
    static void configureButton(juce::ToggleButton& button, const juce::String& style);
    static juce::String formatFrequency(float value);
    static juce::String formatQ(float value);
    static juce::String formatGain(float value);
    static juce::String formatSliderValue(const juce::Slider& slider);
    void applyEqModelPalette();
    int currentEqModelIndex() const;
    void setEqModelIndex(int modelIndex);
    void refreshEqModelButtons();
    void configureStrip(StripControls& strip);
    void addAndRegisterStrip(StripControls& strip);
    bool isLinkEnabled() const;
    bool isMidSideMode() const;
    juce::String stripTitle(int stripIndex) const;
    void attachSliderHandlers(juce::Slider& slider, juce::Slider* linkedSlider = nullptr);
    void attachButtonHandlers(juce::ToggleButton& button, juce::ToggleButton* linkedButton = nullptr);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    NEQAudioProcessor& processor;
    NEQLookAndFeel lookAndFeel;
    juce::Slider* activeSlider = nullptr;
    bool isApplyingLinkedChange = false;
    std::atomic<int> pendingEqModelIndex { NEQ::params::kEqModelDefault };

    std::array<StripControls, NEQ::params::pathCount> strips;

    juce::ToggleButton eqInButton;
    juce::ToggleButton msModeButton;
    juce::ToggleButton linkButton;
    std::array<juce::ToggleButton, NEQ::params::kEqModelCount> eqModelButtons;
    juce::Slider stereoWidthSlider;
    juce::Slider inputGainSlider;
    juce::Slider outputGainSlider;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NEQAudioProcessorEditor)
};