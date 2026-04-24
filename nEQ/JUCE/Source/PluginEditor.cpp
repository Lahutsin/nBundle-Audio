#include "PluginEditor.h"

#include <cmath>
#include "Parameters.h"

namespace
{
constexpr float kUiScale = 0.9f;
constexpr int kGlobalYOffset = 5;
constexpr int kEditorWidth = 418;
constexpr int kEditorHeight = 638;
constexpr int kGainKnobBoundsSize = 67;
constexpr int kQKnobBoundsSize = 67;
constexpr int kFreqKnobBoundsSize = 67;
constexpr int kCenterKnobBoundsSize = 42;
constexpr float kWhiteKnobBodySize = 28.0f;
constexpr float kKnobBodySize = 32.4f;
constexpr int kSquareButtonSize = 28;
constexpr int kMiniButtonSize = 17;
constexpr int kInButtonWidth = 49;
constexpr int kInButtonHeight = 28;
constexpr int kLinkButtonWidth = 49;
constexpr float kKnobInset = 1.0f;
constexpr float kRotaryStart = juce::degreesToRadians(-150.0f);
constexpr float kRotaryEnd = juce::degreesToRadians(150.0f);
constexpr int kModelButtonSpacing = 30;
constexpr int kModelButtonStartY = 273;
constexpr int kVerticalLogoTop = 15;
constexpr int kVerticalLogoHeight = 132;
constexpr int kVerticalLogoWidth = 20;
constexpr int kModelButtonRadioGroup = 0x51514d42;
constexpr int kLayoutOffsetY = -50;
constexpr int kSectionCompactY = 30;
constexpr int kGlobalColumnCentreX = 209;
constexpr std::array<int, NEQ::params::pathCount> kStripOffsetX { 6, 232 };
constexpr int kStripWidth = 180;
constexpr int kStripHeight = 605;

struct GridPoint
{
    int x;
    int y;
};

constexpr int layoutY(int y, int compactSteps = 0)
{
    return y + kLayoutOffsetY - compactSteps * kSectionCompactY + kGlobalYOffset;
}

constexpr int stripX(int pathIndex, int localX)
{
    return kStripOffsetX[static_cast<size_t>(pathIndex)] + localX;
}

constexpr int mirroredLocalX(int pathIndex, int localX)
{
    return pathIndex == 0 ? (kStripWidth - localX) : localX;
}

// red
constexpr GridPoint kHfGainCentre { 51, layoutY(111) };
constexpr GridPoint kHfFreqCentre { 141, layoutY(164) };

// green
constexpr GridPoint kHmfGainCentre { 51, layoutY(238, 1) };
constexpr GridPoint kHmfFreqCentre { 141, layoutY(288, 1) };
constexpr GridPoint kHmfQCentre { 51, layoutY(334, 1) };

// blue
constexpr GridPoint kLmfGainCentre { 51, layoutY(469, 2) };
constexpr GridPoint kLmfFreqCentre { 141, layoutY(511, 2) };
constexpr GridPoint kLmfQCentre { 51, layoutY(573, 2) };

// brown
constexpr GridPoint kLfFreqCentre { 141, layoutY(636, 3) };
constexpr GridPoint kLfGainCentre { 51, layoutY(704, 3) };

// buttons
constexpr GridPoint kHfBellButtonCentre { 140, layoutY(98) };
constexpr GridPoint kHfEnableButtonCentre { 96, layoutY(134) };
constexpr GridPoint kBlackButtonCentre { 140, layoutY(370, 1) };
constexpr GridPoint kHmfEnableButtonCentre { 96, layoutY(288, 1) };
constexpr GridPoint kLmfEnableButtonCentre { 96, layoutY(512, 2) };
constexpr GridPoint kLfBellButtonCentre { 140, layoutY(718, 3) };
constexpr GridPoint kLfEnableButtonCentre { 96, layoutY(666, 3) };
constexpr GridPoint kLinkButtonCentre { kGlobalColumnCentreX, 170 + kGlobalYOffset };
constexpr GridPoint kModeButtonCentre { kGlobalColumnCentreX, 200 + kGlobalYOffset };
constexpr GridPoint kEqInButtonCentre { kGlobalColumnCentreX, 230 + kGlobalYOffset };
constexpr GridPoint kStereoWidthKnobCentre { kGlobalColumnCentreX, 450 + kGlobalYOffset };
constexpr GridPoint kInputGainKnobCentre { kGlobalColumnCentreX, 518 + kGlobalYOffset };
constexpr GridPoint kOutputGainKnobCentre { kGlobalColumnCentreX, 586 + kGlobalYOffset };

juce::Colour panelShadow() { return juce::Colour::fromRGBA(0, 0, 0, 110); }
juce::Colour knobBase() { return juce::Colour::fromRGB(42, 42, 42); }
juce::Colour knobEdge() { return juce::Colour::fromRGB(14, 14, 14); }
juce::Colour knobRim() { return juce::Colour::fromRGB(90, 90, 90); }
juce::Colour knobHighlight() { return juce::Colour::fromRGBA(255, 255, 255, 95); }
juce::Colour buttonFace() { return juce::Colour::fromRGB(216, 216, 214); }
juce::Colour buttonShadow() { return juce::Colour::fromRGBA(0, 0, 0, 95); }
juce::Colour buttonOutline() { return juce::Colour::fromRGB(60, 60, 60); }
juce::Colour buttonText() { return juce::Colour::fromRGB(28, 28, 28); }
juce::Colour stripPanel() { return juce::Colour::fromRGBA(18, 18, 18, 180); }
juce::Colour stripOutline() { return juce::Colour::fromRGBA(255, 255, 255, 24); }
juce::Colour stripHeader() { return juce::Colour::fromRGBA(255, 255, 255, 214); }
juce::Colour backgroundFill() { return juce::Colour::fromRGB(22, 22, 22); }
juce::Colour modeBadgeFill() { return juce::Colour::fromRGB(36, 36, 36); }
juce::Colour modeBadgeOutline() { return juce::Colour::fromRGBA(255, 255, 255, 42); }
juce::Colour logoColour() { return juce::Colour::fromRGBA(255, 255, 255, 165); }
juce::Colour activeWarmTint() { return juce::Colour::fromRGB(255, 214, 112); }

const std::array<const char*, NEQ::params::kEqModelCount> kEqModelButtonLabels { "A", "B", "C", "D", "F" };

struct EqModelPalette
{
    juce::Colour hf;
    juce::Colour hmf;
    juce::Colour lmf;
    juce::Colour lf;
};

EqModelPalette paletteForModel(int modelIndex)
{
    switch (modelIndex)
    {
        case NEQ::params::modelB:
            return {
                juce::Colour::fromRGB(214, 96, 54),
                juce::Colour::fromRGB(122, 177, 56),
                juce::Colour::fromRGB(58, 145, 208),
                juce::Colour::fromRGB(181, 120, 70)
            };

        case NEQ::params::modelC:
            return {
                juce::Colour::fromRGB(162, 74, 58),
                juce::Colour::fromRGB(96, 131, 72),
                juce::Colour::fromRGB(74, 110, 166),
                juce::Colour::fromRGB(124, 94, 84)
            };

        case NEQ::params::modelD:
            return {
                juce::Colour::fromRGB(238, 136, 60),
                juce::Colour::fromRGB(88, 168, 118),
                juce::Colour::fromRGB(72, 174, 214),
                juce::Colour::fromRGB(204, 142, 90)
            };

        case NEQ::params::modelF:
            return {
                juce::Colour::fromRGB(214, 112, 82),
                juce::Colour::fromRGB(138, 158, 78),
                juce::Colour::fromRGB(86, 132, 188),
                juce::Colour::fromRGB(156, 108, 92)
            };

        case NEQ::params::modelA:
        default:
            return {
                juce::Colour::fromRGB(196, 78, 72),
                juce::Colour::fromRGB(85, 158, 84),
                juce::Colour::fromRGB(83, 141, 199),
                juce::Colour::fromRGB(146, 108, 90)
            };
    }
}

juce::Colour knobColour(const juce::Slider& slider)
{
    return juce::Colour::fromString(slider.getProperties()["accent"].toString());
}

juce::Point<int> point(int x, int y)
{
    return { x, y };
}

juce::Rectangle<int> centeredRect(int centreX, int centreY, int width, int height)
{
    const auto centre = point(centreX, centreY);
    return { centre.x - width / 2, centre.y - height / 2, width, height };
}

juce::Rectangle<int> stripBounds(int pathIndex)
{
    return { kStripOffsetX[static_cast<size_t>(pathIndex)], 1 + kGlobalYOffset, kStripWidth, 624 };
}

juce::Path stripOutlinePath(int pathIndex)
{
    const auto bounds = stripBounds(pathIndex).toFloat();
    constexpr float cornerRadius = 8.0f;
    constexpr float insetDepth = 16.0f;
    constexpr float insetTop = 133.0f + static_cast<float>(kGlobalYOffset);
    constexpr float insetBottom = 289.0f + static_cast<float>(kGlobalYOffset);

    juce::Path path;

    if (pathIndex == 0)
    {
        path.startNewSubPath(bounds.getX() + cornerRadius, bounds.getY());
        path.lineTo(bounds.getRight() - cornerRadius, bounds.getY());
        path.quadraticTo(bounds.getRight(), bounds.getY(), bounds.getRight(), bounds.getY() + cornerRadius);
        path.lineTo(bounds.getRight(), insetTop);
        path.lineTo(bounds.getRight() - insetDepth, insetTop + 12.0f);
        path.lineTo(bounds.getRight() - insetDepth, insetBottom - 12.0f);
        path.lineTo(bounds.getRight(), insetBottom);
        path.lineTo(bounds.getRight(), bounds.getBottom() - cornerRadius);
        path.quadraticTo(bounds.getRight(), bounds.getBottom(), bounds.getRight() - cornerRadius, bounds.getBottom());
        path.lineTo(bounds.getX() + cornerRadius, bounds.getBottom());
        path.quadraticTo(bounds.getX(), bounds.getBottom(), bounds.getX(), bounds.getBottom() - cornerRadius);
        path.lineTo(bounds.getX(), bounds.getY() + cornerRadius);
        path.quadraticTo(bounds.getX(), bounds.getY(), bounds.getX() + cornerRadius, bounds.getY());
    }
    else
    {
        path.startNewSubPath(bounds.getX() + cornerRadius, bounds.getY());
        path.lineTo(bounds.getRight() - cornerRadius, bounds.getY());
        path.quadraticTo(bounds.getRight(), bounds.getY(), bounds.getRight(), bounds.getY() + cornerRadius);
        path.lineTo(bounds.getRight(), bounds.getBottom() - cornerRadius);
        path.quadraticTo(bounds.getRight(), bounds.getBottom(), bounds.getRight() - cornerRadius, bounds.getBottom());
        path.lineTo(bounds.getX() + cornerRadius, bounds.getBottom());
        path.quadraticTo(bounds.getX(), bounds.getBottom(), bounds.getX(), bounds.getBottom() - cornerRadius);
        path.lineTo(bounds.getX(), insetBottom);
        path.lineTo(bounds.getX() + insetDepth, insetBottom - 12.0f);
        path.lineTo(bounds.getX() + insetDepth, insetTop + 12.0f);
        path.lineTo(bounds.getX(), insetTop);
        path.lineTo(bounds.getX(), bounds.getY() + cornerRadius);
        path.quadraticTo(bounds.getX(), bounds.getY(), bounds.getX() + cornerRadius, bounds.getY());
    }

    path.closeSubPath();
    return path;
}

juce::Point<float> pointOnArc(juce::Point<float> centre, float angle, float radius)
{
    return { centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius };
}

juce::Colour brighten(juce::Colour colour, float amount)
{
    return colour.interpolatedWith(juce::Colours::white, amount);
}

juce::Colour darken(juce::Colour colour, float amount)
{
    return colour.interpolatedWith(juce::Colours::black, amount);
}

bool isGainKnob(const juce::Slider& slider)
{
    return slider.getProperties()["suffix"].toString() == "dB";
}

bool isQKnob(const juce::Slider& slider)
{
    return slider.getProperties()["valueStyle"].toString() == "q";
}

bool isFrequencyKnob(const juce::Slider& slider)
{
    return slider.getProperties()["contourUnit"].toString().isNotEmpty();
}

void drawContourLabel(juce::Graphics& g, juce::Point<float> centre, const juce::String& text, float fontHeight)
{
    auto textArea = juce::Rectangle<float>(14.0f * kUiScale, 12.0f * kUiScale).withCentre(centre);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::FontOptions(fontHeight, juce::Font::bold));
    g.drawFittedText(text, textArea.toNearestInt(), juce::Justification::centred, 1);
}

void drawKnobContour(juce::Graphics& g, juce::Rectangle<float> area, float rotaryStartAngle, float rotaryEndAngle, bool drawZeroLabel, bool drawEdgeLabels, const juce::String& bottomLabel)
{
    const auto centre = area.getCentre();
    const auto radius = area.getWidth() * 0.5f + 9.0f * kUiScale;
    constexpr float contourRotation = -juce::MathConstants<float>::halfPi;
    constexpr int markerCount = 11;

    for (int index = 0; index < markerCount; ++index)
    {
        const auto proportion = static_cast<float>(index) / static_cast<float>(markerCount - 1);
        const auto angle = rotaryStartAngle + proportion * (rotaryEndAngle - rotaryStartAngle) + contourRotation;
        const auto markerCentre = pointOnArc(centre, angle, radius);

        if (index == 0 || index == markerCount / 2 || index == markerCount - 1)
        {
            if (drawEdgeLabels && index == 0)
            {
                drawContourLabel(g, markerCentre, "-", 11.0f * kUiScale);
                continue;
            }

            if (drawEdgeLabels && index == markerCount - 1)
            {
                drawContourLabel(g, markerCentre, "+", 11.0f * kUiScale);
                continue;
            }

            if (drawZeroLabel)
            {
                drawContourLabel(g, markerCentre, "0", 12.0f * kUiScale);
                continue;
            }
        }

        auto dotArea = juce::Rectangle<float>(5.0f * kUiScale, 5.0f * kUiScale).withCentre(markerCentre);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(dotArea);
    }

    auto dbArea = juce::Rectangle<float>(20.0f * kUiScale, 10.0f * kUiScale).withCentre({ centre.x, centre.y + radius + 2.5f * kUiScale });
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(juce::FontOptions(9.5f * kUiScale, juce::Font::bold));
    g.drawFittedText(bottomLabel, dbArea.toNearestInt(), juce::Justification::centred, 1);
}
}

void NEQLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(kKnobInset);
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = knobColour(slider);
    const auto isWhiteKnob = static_cast<bool>(slider.getProperties()["whiteKnob"]);
    const auto outerSize = isWhiteKnob ? kWhiteKnobBodySize : kKnobBodySize;

    const auto centre = isWhiteKnob
        ? juce::Point<float>(area.getCentreX(), area.getY() + outerSize * 0.5f + 1.0f)
        : area.getCentre();

    const auto outer = juce::Rectangle<float>(outerSize, outerSize).withCentre(centre);

    if (isWhiteKnob)
    {
        juce::ColourGradient faceGradient(juce::Colour::fromRGB(242, 242, 240),
                                          outer.getTopLeft(),
                                          juce::Colour::fromRGB(194, 194, 192),
                                          outer.getBottomRight(),
                                          false);
        g.setGradientFill(faceGradient);
        g.fillEllipse(outer);

        g.setColour(buttonOutline());
        g.drawEllipse(outer, 1.2f);

        const auto pointerLength = outer.getHeight() * 0.36f;
        juce::Path pointer;
        pointer.startNewSubPath(0.0f, 0.0f);
        pointer.lineTo(0.0f, -pointerLength);
        g.setColour(juce::Colours::black.withAlpha(0.9f));
        g.strokePath(pointer,
                     juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

        const auto caption = slider.getProperties()["caption"].toString();
        if (caption.isNotEmpty())
        {
            auto captionArea = juce::Rectangle<float>(32.0f * kUiScale, 10.0f * kUiScale)
                .withCentre({ area.getCentreX(), area.getBottom() - 5.0f });
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.setFont(juce::FontOptions(9.0f * kUiScale, juce::Font::bold));
            g.drawFittedText(caption, captionArea.toNearestInt(), juce::Justification::centred, 1);
        }

        return;
    }

    if (isGainKnob(slider))
        drawKnobContour(g, outer, rotaryStartAngle, rotaryEndAngle, true, true, "dB");
    else if (isQKnob(slider))
        drawKnobContour(g, outer, rotaryStartAngle, rotaryEndAngle, false, false, "Q");
    else if (isFrequencyKnob(slider))
        drawKnobContour(g, outer, rotaryStartAngle, rotaryEndAngle, false, false, slider.getProperties()["contourUnit"].toString());

    g.setColour(darken(accent, 0.18f));
    g.fillEllipse(outer);

    g.setColour(juce::Colour::fromRGB(24, 24, 24));
    g.drawEllipse(outer, 1.5f);

    const auto pointerLength = outer.getHeight() * 0.38f;
    juce::Path pointer;
    pointer.startNewSubPath(0.0f, 0.0f);
    pointer.lineTo(0.0f, -pointerLength);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.strokePath(pointer,
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void NEQLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto area = button.getLocalBounds().toFloat();
    const auto active = button.getToggleState();
    const auto style = button.getProperties()["style"].toString();

    for (int shadowIndex = 0; shadowIndex < 4; ++shadowIndex)
    {
        const auto shadowArea = area.translated(0.0f, 2.0f + static_cast<float>(shadowIndex))
                                     .expanded(static_cast<float>(shadowIndex) * 0.35f);
        g.setColour(buttonShadow().withAlpha(0.2f - static_cast<float>(shadowIndex) * 0.03f));
        g.fillRoundedRectangle(shadowArea, style == "in" ? 3.0f : 2.4f);
    }

    auto face = area.reduced(0.5f);
    const auto activeTop = juce::Colour::fromRGB(242, 242, 240).interpolatedWith(activeWarmTint(), 0.05f);
    const auto activeBottom = juce::Colour::fromRGB(194, 194, 192).interpolatedWith(activeWarmTint(), 0.05f);

    juce::ColourGradient faceGradient(active ? activeTop : juce::Colour::fromRGB(192, 192, 190),
                                      face.getTopLeft(),
                                      active ? activeBottom : juce::Colour::fromRGB(142, 142, 140),
                                      face.getBottomRight(),
                                      false);
    g.setGradientFill(faceGradient);
    g.fillRoundedRectangle(face, style == "in" ? 3.0f : 2.4f);

    g.setColour(buttonOutline());
    g.drawRoundedRectangle(face, style == "in" ? 3.0f : 2.4f, 1.0f);

    g.setColour(buttonText());
    g.setFont(juce::FontOptions(15.0f * kUiScale, juce::Font::bold));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
}

NEQAudioProcessorEditor::NEQAudioProcessorEditor(NEQAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor(&processorToEdit)
    , processor(processorToEdit)
{
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);
    setSize(kEditorWidth, kEditorHeight);

    for (auto& strip : strips)
    {
        configureStrip(strip);
        addAndRegisterStrip(strip);
    }

    configureButton(eqInButton, "in");
    eqInButton.setButtonText("IN");
    addAndMakeVisible(eqInButton);

    configureButton(msModeButton, "in");
    msModeButton.setButtonText("M/S");
    addAndMakeVisible(msModeButton);

    configureButton(linkButton, "in");
    linkButton.setButtonText("LINK");
    addAndMakeVisible(linkButton);

    configureKnob(stereoWidthSlider, juce::Colour::fromRGB(220, 220, 220), "V", true);
    stereoWidthSlider.getProperties().set("caption", "V");
    addAndMakeVisible(stereoWidthSlider);

    configureKnob(inputGainSlider, juce::Colour::fromRGB(220, 220, 220), "dB", true);
    inputGainSlider.getProperties().set("caption", "In");
    addAndMakeVisible(inputGainSlider);

    configureKnob(outputGainSlider, juce::Colour::fromRGB(220, 220, 220), "dB", true);
    outputGainSlider.getProperties().set("caption", "Out");
    addAndMakeVisible(outputGainSlider);

    for (int modelIndex = 0; modelIndex < NEQ::params::kEqModelCount; ++modelIndex)
    {
        auto& button = eqModelButtons[static_cast<size_t>(modelIndex)];
        configureButton(button, "rect");
        button.setButtonText(kEqModelButtonLabels[static_cast<size_t>(modelIndex)]);
        button.setRadioGroupId(kModelButtonRadioGroup);
        button.onClick = [this, modelIndex, &button]
        {
            if (button.getToggleState())
                setEqModelIndex(modelIndex);
            else
                refreshEqModelButtons();
        };
        addAndMakeVisible(button);
    }

    auto& state = processor.getState();
    state.addParameterListener(NEQ::params::eqModel, this);

    for (int pathIndex = 0; pathIndex < NEQ::params::pathCount; ++pathIndex)
    {
        auto ids = NEQ::params::idsForPath(pathIndex);
        auto& strip = strips[static_cast<size_t>(pathIndex)];

        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.hfGain, strip.hfGainSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.hfFreq, strip.hfFreqSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.hmfGain, strip.hmfGainSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.hmfFreq, strip.hmfFreqSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.hmfQ, strip.hmfQSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.lmfGain, strip.lmfGainSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.lmfFreq, strip.lmfFreqSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.lmfQ, strip.lmfQSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.lfGain, strip.lfGainSlider));
        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, ids.lfFreq, strip.lfFreqSlider));

        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.hfBell, strip.hfBellButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.hfEnable, strip.hfEnableButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.blackMode, strip.blackButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.hmfEnable, strip.hmfEnableButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.lmfEnable, strip.lmfEnableButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.lfBell, strip.lfBellButton));
        buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, ids.lfEnable, strip.lfEnableButton));
    }

    buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, NEQ::params::eqIn, eqInButton));
    buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, NEQ::params::msMode, msModeButton));
    buttonAttachments.emplace_back(std::make_unique<ButtonAttachment>(state, NEQ::params::linkChannels, linkButton));
    sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, NEQ::params::stereoWidth, stereoWidthSlider));
    sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, NEQ::params::inputGain, inputGainSlider));
    sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(state, NEQ::params::outputGain, outputGainSlider));

    auto attachLinkedSliderPair = [this] (juce::Slider& first, juce::Slider& second)
    {
        attachSliderHandlers(first, &second);
        attachSliderHandlers(second, &first);
    };

    auto attachLinkedButtonPair = [this] (juce::ToggleButton& first, juce::ToggleButton& second)
    {
        attachButtonHandlers(first, &second);
        attachButtonHandlers(second, &first);
    };

    attachLinkedSliderPair(strips[0].hfGainSlider, strips[1].hfGainSlider);
    attachLinkedSliderPair(strips[0].hfFreqSlider, strips[1].hfFreqSlider);
    attachLinkedSliderPair(strips[0].hmfGainSlider, strips[1].hmfGainSlider);
    attachLinkedSliderPair(strips[0].hmfFreqSlider, strips[1].hmfFreqSlider);
    attachLinkedSliderPair(strips[0].hmfQSlider, strips[1].hmfQSlider);
    attachLinkedSliderPair(strips[0].lmfGainSlider, strips[1].lmfGainSlider);
    attachLinkedSliderPair(strips[0].lmfFreqSlider, strips[1].lmfFreqSlider);
    attachLinkedSliderPair(strips[0].lmfQSlider, strips[1].lmfQSlider);
    attachLinkedSliderPair(strips[0].lfGainSlider, strips[1].lfGainSlider);
    attachLinkedSliderPair(strips[0].lfFreqSlider, strips[1].lfFreqSlider);

    attachLinkedButtonPair(strips[0].hfBellButton, strips[1].hfBellButton);
    attachLinkedButtonPair(strips[0].hfEnableButton, strips[1].hfEnableButton);
    attachLinkedButtonPair(strips[0].blackButton, strips[1].blackButton);
    attachLinkedButtonPair(strips[0].hmfEnableButton, strips[1].hmfEnableButton);
    attachLinkedButtonPair(strips[0].lmfEnableButton, strips[1].lmfEnableButton);
    attachLinkedButtonPair(strips[0].lfBellButton, strips[1].lfBellButton);
    attachLinkedButtonPair(strips[0].lfEnableButton, strips[1].lfEnableButton);

    attachButtonHandlers(eqInButton);
    attachButtonHandlers(msModeButton);
    attachButtonHandlers(linkButton);
    attachSliderHandlers(stereoWidthSlider);
    attachSliderHandlers(inputGainSlider);
    attachSliderHandlers(outputGainSlider);

    pendingEqModelIndex.store(currentEqModelIndex());
    refreshEqModelButtons();
    applyEqModelPalette();
}

NEQAudioProcessorEditor::~NEQAudioProcessorEditor()
{
    processor.getState().removeParameterListener(NEQ::params::eqModel, this);
    cancelPendingUpdate();
    setLookAndFeel(nullptr);
}

void NEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(backgroundFill());

    auto logoArea = juce::Rectangle<int>(kGlobalColumnCentreX - kVerticalLogoWidth / 2,
                                         kVerticalLogoTop,
                                         kVerticalLogoWidth,
                                         kVerticalLogoHeight);
    g.setColour(logoColour());
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawFittedText("Q\n2\nM\nB\nR\n-\nE\nQ", logoArea, juce::Justification::centred, 8);

    for (int pathIndex = 0; pathIndex < NEQ::params::pathCount; ++pathIndex)
    {
        g.setColour(stripOutline());
        g.strokePath(stripOutlinePath(pathIndex), juce::PathStrokeType(1.0f));

        const auto badgeCentreX = stripX(pathIndex, mirroredLocalX(pathIndex, kBlackButtonCentre.x));
        const auto badgeCentreY = kBlackButtonCentre.y + 40;
        auto badgeArea = juce::Rectangle<float>(22.0f, 22.0f).withCentre({ static_cast<float>(badgeCentreX), static_cast<float>(badgeCentreY) });
        g.setColour(modeBadgeFill());
        g.fillEllipse(badgeArea);
        g.setColour(modeBadgeOutline());
        g.drawEllipse(badgeArea, 1.0f);
        g.setColour(stripHeader());
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.drawFittedText(stripTitle(pathIndex), badgeArea.toNearestInt().translated(0, -1), juce::Justification::centred, 1);
    }

    if (activeSlider != nullptr)
    {
        auto valueArea = activeSlider->getBounds().withY(activeSlider->getBottom() + 6).withHeight(18).expanded(8, 0);
        valueArea = valueArea.withX(juce::jlimit(8, getWidth() - valueArea.getWidth() - 8, valueArea.getX()));

        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRoundedRectangle(valueArea.toFloat(), 5.0f);

        g.setColour(juce::Colour::fromRGB(228, 228, 228));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawFittedText(formatSliderValue(*activeSlider), valueArea, juce::Justification::centred, 1);
    }
}

void NEQAudioProcessorEditor::resized()
{
    eqInButton.setBounds(centeredRect(kEqInButtonCentre.x, kEqInButtonCentre.y, kInButtonWidth, kInButtonHeight));
    msModeButton.setBounds(centeredRect(kModeButtonCentre.x, kModeButtonCentre.y, kInButtonWidth, kInButtonHeight));
    linkButton.setBounds(centeredRect(kLinkButtonCentre.x, kLinkButtonCentre.y, kLinkButtonWidth, kInButtonHeight));

    for (int modelIndex = 0; modelIndex < NEQ::params::kEqModelCount; ++modelIndex)
    {
        eqModelButtons[static_cast<size_t>(modelIndex)].setBounds(centeredRect(kGlobalColumnCentreX,
                                                                                kModelButtonStartY + modelIndex * kModelButtonSpacing,
                                                                                kSquareButtonSize,
                                                                                kSquareButtonSize));
    }

    stereoWidthSlider.setBounds(centeredRect(kStereoWidthKnobCentre.x, kStereoWidthKnobCentre.y, kCenterKnobBoundsSize, kCenterKnobBoundsSize));
    inputGainSlider.setBounds(centeredRect(kInputGainKnobCentre.x, kInputGainKnobCentre.y, kCenterKnobBoundsSize, kCenterKnobBoundsSize));
    outputGainSlider.setBounds(centeredRect(kOutputGainKnobCentre.x, kOutputGainKnobCentre.y, kCenterKnobBoundsSize, kCenterKnobBoundsSize));

    for (int pathIndex = 0; pathIndex < NEQ::params::pathCount; ++pathIndex)
    {
        auto& strip = strips[static_cast<size_t>(pathIndex)];

        strip.hfGainSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHfGainCentre.x)), kHfGainCentre.y, kGainKnobBoundsSize, kGainKnobBoundsSize));
        strip.hfFreqSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHfFreqCentre.x)), kHfFreqCentre.y, kFreqKnobBoundsSize, kFreqKnobBoundsSize));

        strip.hmfGainSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHmfGainCentre.x)), kHmfGainCentre.y, kGainKnobBoundsSize, kGainKnobBoundsSize));
        strip.hmfFreqSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHmfFreqCentre.x)), kHmfFreqCentre.y, kFreqKnobBoundsSize, kFreqKnobBoundsSize));
        strip.hmfQSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHmfQCentre.x)), kHmfQCentre.y, kQKnobBoundsSize, kQKnobBoundsSize));

        strip.lmfGainSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLmfGainCentre.x)), kLmfGainCentre.y, kGainKnobBoundsSize, kGainKnobBoundsSize));
        strip.lmfFreqSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLmfFreqCentre.x)), kLmfFreqCentre.y, kFreqKnobBoundsSize, kFreqKnobBoundsSize));
        strip.lmfQSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLmfQCentre.x)), kLmfQCentre.y, kQKnobBoundsSize, kQKnobBoundsSize));

        strip.lfFreqSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLfFreqCentre.x)), kLfFreqCentre.y, kFreqKnobBoundsSize, kFreqKnobBoundsSize));
        strip.lfGainSlider.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLfGainCentre.x)), kLfGainCentre.y, kGainKnobBoundsSize, kGainKnobBoundsSize));

        strip.hfBellButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHfBellButtonCentre.x)), kHfBellButtonCentre.y, kSquareButtonSize, kSquareButtonSize));
        strip.hfEnableButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHfEnableButtonCentre.x)), kHfEnableButtonCentre.y, kMiniButtonSize, kMiniButtonSize));
        strip.blackButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kBlackButtonCentre.x)), kBlackButtonCentre.y, kSquareButtonSize, kSquareButtonSize));
        strip.hmfEnableButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kHmfEnableButtonCentre.x)), kHmfEnableButtonCentre.y, kMiniButtonSize, kMiniButtonSize));
        strip.lmfEnableButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLmfEnableButtonCentre.x)), kLmfEnableButtonCentre.y, kMiniButtonSize, kMiniButtonSize));
        strip.lfBellButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLfBellButtonCentre.x)), kLfBellButtonCentre.y, kSquareButtonSize, kSquareButtonSize));
        strip.lfEnableButton.setBounds(centeredRect(stripX(pathIndex, mirroredLocalX(pathIndex, kLfEnableButtonCentre.x)), kLfEnableButtonCentre.y, kMiniButtonSize, kMiniButtonSize));
    }
}

void NEQAudioProcessorEditor::configureKnob(juce::Slider& slider, juce::Colour colour, const juce::String& suffix, bool whiteStyle)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(kRotaryStart, kRotaryEnd, true);
    slider.setMouseDragSensitivity(170);
    slider.setPopupDisplayEnabled(false, false, nullptr);
    slider.getProperties().set("accent", colour.toString());
    slider.getProperties().set("suffix", suffix);
    slider.getProperties().set("whiteKnob", whiteStyle);
}

void NEQAudioProcessorEditor::configureButton(juce::ToggleButton& button, const juce::String& style)
{
    button.setClickingTogglesState(true);
    button.getProperties().set("style", style);
}

void NEQAudioProcessorEditor::configureStrip(StripControls& strip)
{
    configureKnob(strip.hfGainSlider, juce::Colour::fromRGB(196, 78, 72), "dB");
    configureKnob(strip.hfFreqSlider, juce::Colour::fromRGB(196, 78, 72));
    strip.hfFreqSlider.getProperties().set("contourUnit", "kHz");

    configureKnob(strip.hmfGainSlider, juce::Colour::fromRGB(85, 158, 84), "dB");
    configureKnob(strip.hmfFreqSlider, juce::Colour::fromRGB(85, 158, 84));
    strip.hmfFreqSlider.getProperties().set("contourUnit", "kHz");
    configureKnob(strip.hmfQSlider, juce::Colour::fromRGB(85, 158, 84));
    strip.hmfQSlider.getProperties().set("valueStyle", "q");

    configureKnob(strip.lmfGainSlider, juce::Colour::fromRGB(83, 141, 199), "dB");
    configureKnob(strip.lmfFreqSlider, juce::Colour::fromRGB(83, 141, 199));
    strip.lmfFreqSlider.getProperties().set("contourUnit", "kHz");
    configureKnob(strip.lmfQSlider, juce::Colour::fromRGB(83, 141, 199));
    strip.lmfQSlider.getProperties().set("valueStyle", "q");

    configureKnob(strip.lfGainSlider, juce::Colour::fromRGB(146, 108, 90), "dB");
    configureKnob(strip.lfFreqSlider, juce::Colour::fromRGB(146, 108, 90));
    strip.lfFreqSlider.getProperties().set("contourUnit", "Hz");

    configureButton(strip.hfBellButton, "rect");
    strip.hfBellButton.setButtonText("BL");
    configureButton(strip.hfEnableButton, "rect");
    strip.hfEnableButton.setButtonText({});

    configureButton(strip.blackButton, "rect");
    strip.blackButton.setButtonText("BK");

    configureButton(strip.hmfEnableButton, "rect");
    strip.hmfEnableButton.setButtonText({});

    configureButton(strip.lmfEnableButton, "rect");
    strip.lmfEnableButton.setButtonText({});

    configureButton(strip.lfBellButton, "rect");
    strip.lfBellButton.setButtonText("BL");
    configureButton(strip.lfEnableButton, "rect");
    strip.lfEnableButton.setButtonText({});
}

void NEQAudioProcessorEditor::applyEqModelPalette()
{
    const auto palette = paletteForModel(pendingEqModelIndex.load());

    for (auto& strip : strips)
    {
        strip.hfGainSlider.getProperties().set("accent", palette.hf.toString());
        strip.hfFreqSlider.getProperties().set("accent", palette.hf.toString());
        strip.hmfGainSlider.getProperties().set("accent", palette.hmf.toString());
        strip.hmfFreqSlider.getProperties().set("accent", palette.hmf.toString());
        strip.hmfQSlider.getProperties().set("accent", palette.hmf.toString());
        strip.lmfGainSlider.getProperties().set("accent", palette.lmf.toString());
        strip.lmfFreqSlider.getProperties().set("accent", palette.lmf.toString());
        strip.lmfQSlider.getProperties().set("accent", palette.lmf.toString());
        strip.lfGainSlider.getProperties().set("accent", palette.lf.toString());
        strip.lfFreqSlider.getProperties().set("accent", palette.lf.toString());
    }
}

void NEQAudioProcessorEditor::addAndRegisterStrip(StripControls& strip)
{
    for (auto* slider : { &strip.hfGainSlider, &strip.hfFreqSlider, &strip.hmfGainSlider, &strip.hmfFreqSlider, &strip.hmfQSlider,
                          &strip.lmfGainSlider, &strip.lmfFreqSlider, &strip.lmfQSlider, &strip.lfGainSlider, &strip.lfFreqSlider })
        addAndMakeVisible(*slider);

    for (auto* button : { &strip.hfBellButton, &strip.hfEnableButton, &strip.blackButton, &strip.hmfEnableButton,
                          &strip.lmfEnableButton, &strip.lfBellButton, &strip.lfEnableButton })
        addAndMakeVisible(*button);
}

bool NEQAudioProcessorEditor::isLinkEnabled() const
{
    if (auto* value = processor.getState().getRawParameterValue(NEQ::params::linkChannels))
        return value->load() >= 0.5f;

    return false;
}

bool NEQAudioProcessorEditor::isMidSideMode() const
{
    if (auto* value = processor.getState().getRawParameterValue(NEQ::params::msMode))
        return value->load() >= 0.5f;

    return false;
}

juce::String NEQAudioProcessorEditor::stripTitle(int stripIndex) const
{
    if (isMidSideMode())
        return stripIndex == 0 ? "M" : "S";

    return stripIndex == 0 ? "L" : "R";
}

void NEQAudioProcessorEditor::attachSliderHandlers(juce::Slider& slider, juce::Slider* linkedSlider)
{
    slider.onDragStart = [this, &slider]
    {
        activeSlider = &slider;
        repaint();
    };

    slider.onDragEnd = [this, &slider]
    {
        if (activeSlider == &slider)
            activeSlider = nullptr;

        repaint();
    };

    slider.onValueChange = [this, &slider, linkedSlider]
    {
        if (activeSlider == &slider)
            repaint();

        if (linkedSlider == nullptr || !isLinkEnabled() || isApplyingLinkedChange)
            return;

        if (!slider.isMouseButtonDown() && activeSlider != &slider)
            return;

        const juce::ScopedValueSetter<bool> guard(isApplyingLinkedChange, true);
        linkedSlider->setValue(slider.getValue(), juce::sendNotificationSync);
    };
}

void NEQAudioProcessorEditor::attachButtonHandlers(juce::ToggleButton& button, juce::ToggleButton* linkedButton)
{
    button.onClick = [this, &button, linkedButton]
    {
        repaint();

        if (linkedButton == nullptr || !isLinkEnabled() || isApplyingLinkedChange)
            return;

        const juce::ScopedValueSetter<bool> guard(isApplyingLinkedChange, true);
        linkedButton->setToggleState(button.getToggleState(), juce::sendNotificationSync);
    };
}

juce::String NEQAudioProcessorEditor::formatFrequency(float value)
{
    if (value >= 1000.0f)
        return juce::String(value / 1000.0f, 2) + " kHz";

    return juce::String(value, 2) + " Hz";
}

juce::String NEQAudioProcessorEditor::formatQ(float value)
{
    return "Q " + juce::String(value, 1);
}

juce::String NEQAudioProcessorEditor::formatGain(float value)
{
    return juce::String(value, 2) + " dB";
}

juce::String NEQAudioProcessorEditor::formatSliderValue(const juce::Slider& slider)
{
    if (slider.getProperties()["caption"].toString() == "V")
        return juce::String(static_cast<float>(slider.getValue()), 2) + " V";

    if (slider.getProperties()["suffix"].toString() == "dB")
        return formatGain(static_cast<float>(slider.getValue()));

    if (slider.getProperties()["valueStyle"].toString() == "q")
        return formatQ(static_cast<float>(slider.getValue()));

    return formatFrequency(static_cast<float>(slider.getValue()));
}

int NEQAudioProcessorEditor::currentEqModelIndex() const
{
    if (auto* value = processor.getState().getRawParameterValue(NEQ::params::eqModel))
        return juce::jlimit(0, NEQ::params::kEqModelCount - 1, juce::roundToInt(value->load()));

    return NEQ::params::kEqModelDefault;
}

void NEQAudioProcessorEditor::setEqModelIndex(int modelIndex)
{
    modelIndex = juce::jlimit(0, NEQ::params::kEqModelCount - 1, modelIndex);

    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processor.getState().getParameter(NEQ::params::eqModel)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(modelIndex)));
        parameter->endChangeGesture();
    }
}

void NEQAudioProcessorEditor::refreshEqModelButtons()
{
    const auto modelIndex = pendingEqModelIndex.load();

    for (int index = 0; index < NEQ::params::kEqModelCount; ++index)
        eqModelButtons[static_cast<size_t>(index)].setToggleState(index == modelIndex, juce::dontSendNotification);
}

void NEQAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == NEQ::params::eqModel)
    {
        pendingEqModelIndex.store(juce::jlimit(0, NEQ::params::kEqModelCount - 1, juce::roundToInt(newValue)));
        triggerAsyncUpdate();
    }
}

void NEQAudioProcessorEditor::handleAsyncUpdate()
{
    refreshEqModelButtons();
    applyEqModelPalette();
    repaint();
}