#include "PluginEditor.h"

#include <thread>
#include "Parameters.h"
#include <array>
#include <cmath>

namespace
{
constexpr int kManualPresetItemId = 1;
constexpr int kFactoryPresetItemBase = 1000;
constexpr int kUserPresetItemBase = 2000;
constexpr int kOuterMarginX = 5;
constexpr int kLegacyOuterMarginX = 34;

constexpr int shiftLayoutX(int value)
{
    return value - (kLegacyOuterMarginX - kOuterMarginX);
}

struct Layout
{
    static constexpr int width = 1189 - (kLegacyOuterMarginX - kOuterMarginX) * 2;
    static constexpr int height = 344;
    static constexpr int bodyPanelTop = 56;
    static constexpr int knobSlotSize = 76;
    static constexpr int knobSize = 43;

    static constexpr int channelPanelX = shiftLayoutX(34);
    static constexpr int channelPanelW = 610;
    static constexpr int channelPanelH = 128;
    static constexpr int topChannelPanelY = 68;
    static constexpr int bottomChannelPanelY = 202;

    static constexpr int mixPanelX = shiftLayoutX(650);
    static constexpr int mixPanelY = 68;
    static constexpr int mixPanelW = 102;
    static constexpr int mixPanelH = 262;

    static constexpr int characterX = shiftLayoutX(663);
    static constexpr int characterY = 94;

    static constexpr int in1X = shiftLayoutX(60);
    static constexpr int in1Y = 94;
    static constexpr int in2X = shiftLayoutX(60);
    static constexpr int in2Y = 228;

    static constexpr int attack1X = shiftLayoutX(156);
    static constexpr int attack1Y = 94;
    static constexpr int attack2X = shiftLayoutX(156);
    static constexpr int attack2Y = 228;

    static constexpr int release1X = shiftLayoutX(252);
    static constexpr int release1Y = 94;
    static constexpr int release2X = shiftLayoutX(252);
    static constexpr int release2Y = 228;

    static constexpr int thresh1X = shiftLayoutX(348);
    static constexpr int thresh1Y = 94;
    static constexpr int thresh2X = shiftLayoutX(348);
    static constexpr int thresh2Y = 228;

    static constexpr int ratio1X = shiftLayoutX(444);
    static constexpr int ratio1Y = 94;
    static constexpr int ratio2X = shiftLayoutX(444);
    static constexpr int ratio2Y = 228;

    static constexpr int out1X = shiftLayoutX(540);
    static constexpr int out1Y = 94;
    static constexpr int out2X = shiftLayoutX(540);
    static constexpr int out2Y = 228;

    static constexpr int mixX = shiftLayoutX(663);
    static constexpr int mixY = 228;

    static constexpr int utilityPanelX = shiftLayoutX(758);
    static constexpr int utilityPanelY = 68;
    static constexpr int utilityPanelW = 170;
    static constexpr int utilityPanelH = 262;
    static constexpr int utilityLabelW = 52;
    static constexpr int utilityRowH = 12;
    static constexpr int utilityTransferH = 12;

    static constexpr int buttonGap = 6;
    static constexpr int toggleW = 72;
    static constexpr int toggleH = 28;
    static constexpr int linkX = utilityPanelX + 10;
    static constexpr int linkY = utilityPanelY + 184;
    static constexpr int listenX = linkX + toggleW + buttonGap;
    static constexpr int listenY = linkY;
    static constexpr int scX = linkX;
    static constexpr int scY = linkY + toggleH + buttonGap;
    static constexpr int powerX = listenX;
    static constexpr int powerY = scY;
    static constexpr int mslrX = utilityPanelX + 10;
    static constexpr int mslrY = powerY + toggleH + buttonGap;
    static constexpr int mslrW = utilityPanelW - 20;
    static constexpr int mslrH = 28;

    static constexpr int meterHousingX = shiftLayoutX(937);
    static constexpr int meterHousingY = 68;
    static constexpr int meterHousingW = 218;
    static constexpr int meterHousingH = 262;
    static constexpr int meterWindowX = shiftLayoutX(948);
    static constexpr int meterWindowW = 196;
    static constexpr int meterWindowH = 92;
    static constexpr int meterWindowEdgeInset = 18;
    static constexpr int topKnobRowCentreY = in1Y + knobSlotSize / 2;
    static constexpr int bottomKnobRowCentreY = in2Y + knobSlotSize / 2;
    static constexpr int meterTopY = meterHousingY + meterWindowEdgeInset;
    static constexpr int meterBottomY = meterHousingY + meterHousingH - meterWindowEdgeInset - meterWindowH;

    static constexpr int toolbarX = channelPanelX;
    static constexpr int toolbarY = 8;
    static constexpr int toolbarW = meterHousingX + meterHousingW - toolbarX;
    static constexpr int toolbarH = 41;
    static constexpr int toolbarInsetX = 10;
    static constexpr int toolbarArrowW = 32;
    static constexpr int toolbarButtonH = 26;
    static constexpr int toolbarActionW = 56;
    static constexpr int toolbarGap = 6;
    static constexpr int toolbarSectionGap = 12;
    static constexpr int toolbarCopyW = 32;
    static constexpr int toolbarABW = 32;
    static constexpr int toolbarPowerW = toolbarABW;
    static constexpr int toolbarPowerH = toolbarButtonH;
};

constexpr float legendStartAngle = 140.0f * juce::MathConstants<float>::pi / 180.0f;
constexpr float legendEndAngle = 400.0f * juce::MathConstants<float>::pi / 180.0f;
constexpr float meterStartAngle = 200.0f * juce::MathConstants<float>::pi / 180.0f;
constexpr float meterEndAngle = 340.0f * juce::MathConstants<float>::pi / 180.0f;
constexpr float knobMetricScale = static_cast<float>(Layout::knobSize) / 54.0f;

constexpr float scaleKnobMetric(float value)
{
    return value * knobMetricScale;
}

constexpr float knobTickInnerRadius = scaleKnobMetric(31.0f);
constexpr float knobTickOuterRadius = scaleKnobMetric(34.0f);
constexpr float knobLabelRadiusWide = scaleKnobMetric(46.0f);
constexpr float knobLabelRadiusDense = scaleKnobMetric(48.0f);
constexpr float knobLabelRadiusMix = scaleKnobMetric(43.0f);
constexpr float knobCaptionOffsetY = scaleKnobMetric(17.0f);
constexpr float knobCaptionWidth = scaleKnobMetric(96.0f);
constexpr float knobCaptionHeight = scaleKnobMetric(14.0f);
constexpr float knobValueHeight = scaleKnobMetric(18.0f);
constexpr float knobValueMinWidth = scaleKnobMetric(68.0f);
constexpr float knobEditorMinWidth = scaleKnobMetric(62.0f);
constexpr float knobEditorMaxWidth = scaleKnobMetric(82.0f);
constexpr float knobLegendMinWidth = scaleKnobMetric(28.0f);
constexpr double knobWholeValueTolerance = 0.0005;

constexpr std::array<const char*, 3> bipolarScale { "-24", "0", "24" };
constexpr std::array<const char*, 5> attackScale { "1", "10", "40", "160", "640" };
constexpr std::array<const char*, 5> releaseScale { "5", "50", "200", "1K", "5K" };
constexpr std::array<const char*, 5> thresholdScale { "-32", "-24", "-16", "-8", "0" };
constexpr std::array<const char*, 5> ratioScale { "x2", "x4", "x6", "x8", "x9" };
constexpr std::array<const char*, 3> mixScale { "DRY", "50 %", "WET" };
constexpr std::array<const char*, 7> meterScale { "0", "4", "8", "12", "16", "20", "24" };

constexpr std::array<float, 3> bipolarScaleProportions { 0.0f, 0.5f, 1.0f };
constexpr std::array<float, 5> attackScaleProportions { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
constexpr std::array<float, 5> releaseScaleProportions { 0.0f, 0.125f, 0.375f, 0.5f, 1.0f };
constexpr std::array<float, 5> thresholdScaleProportions { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
constexpr std::array<float, 5> ratioScaleProportions { 0.0f, 2.0f / 7.0f, 4.0f / 7.0f, 6.0f / 7.0f, 1.0f };
constexpr std::array<float, 3> mixScaleProportions { 0.0f, 0.5f, 1.0f };

juce::Font makeFont(float height, int styleFlags = juce::Font::plain)
{
    return juce::Font(juce::FontOptions(height, styleFlags));
}

juce::Colour lightenDarkSurface(juce::Colour colour)
{
    constexpr float liftAmount = 0.10f;
    const auto liftChannel = [] (float channel)
    {
        return juce::jlimit(0.0f, 1.0f, channel + (1.0f - channel) * liftAmount);
    };

    return juce::Colour::fromFloatRGBA(liftChannel(colour.getFloatRed()),
                                       liftChannel(colour.getFloatGreen()),
                                       liftChannel(colour.getFloatBlue()),
                                       colour.getFloatAlpha());
}

juce::Point<float> pointOnCircle(juce::Point<float> centre, float angle, float radius)
{
    return { centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius };
}

juce::Point<float> meterPivot(const juce::Rectangle<float>& area)
{
    return { area.getCentreX(), area.getBottom() - 8.0f };
}

float meterArcInnerRadius(const juce::Rectangle<float>& area)
{
    return juce::jmin(area.getWidth() * 0.265f, area.getHeight() * 0.60f);
}

float meterArcOuterRadius(const juce::Rectangle<float>& area)
{
    return meterArcInnerRadius(area) + 7.0f;
}

float meterLabelRadius(const juce::Rectangle<float>& area)
{
    return meterArcOuterRadius(area) + 6.0f;
}

float meterNeedleRadius(const juce::Rectangle<float>& area)
{
    return meterArcOuterRadius(area) - 1.5f;
}

juce::Rectangle<float> meterGlassArea(const juce::Rectangle<float>& area)
{
    return area.reduced(9.0f, 8.0f);
}

juce::Point<float> knobCentre(int baseX, int baseY, int offsetX, int offsetY)
{
    return { static_cast<float>(baseX + offsetX + Layout::knobSlotSize / 2),
             static_cast<float>(baseY + offsetY + Layout::knobSlotSize / 2) };
}

juce::Rectangle<float> knobCaptionArea(const juce::Slider& slider)
{
    const auto bounds = slider.getBounds().toFloat();
    return { bounds.getCentreX() - knobCaptionWidth * 0.5f,
             bounds.getBottom() + knobCaptionOffsetY,
             knobCaptionWidth,
             knobCaptionHeight };
}

juce::Rectangle<float> knobValueArea(const juce::Slider& slider, float width)
{
    const auto captionArea = knobCaptionArea(slider);
    return juce::Rectangle<float>(width, knobValueHeight).withCentre({ captionArea.getCentreX(), captionArea.getY() + knobValueHeight * 0.5f });
}

bool isWholeKnobValue(double value)
{
    return std::abs(value - std::round(value)) < knobWholeValueTolerance;
}

double normaliseKnobValue(double value, const juce::String& suffix)
{
    if (suffix == "%")
        return std::round(value);

    if (isWholeKnobValue(value))
        return std::round(value);

    return std::round(value * 100.0) / 100.0;
}

juce::String formatKnobValue(double rawValue, const juce::String& suffix, bool includeSuffix)
{
    const auto normalisedValue = normaliseKnobValue(rawValue, suffix);
    const int decimals = isWholeKnobValue(normalisedValue) ? 0 : 2;

    juce::String text(normalisedValue, decimals);
    if (includeSuffix && suffix.isNotEmpty())
        text << " " << suffix;

    return text;
}

float knobEditorWidth(const juce::String& text)
{
    return juce::jlimit(knobEditorMinWidth,
                        knobEditorMaxWidth,
                        scaleKnobMetric(18.0f) + static_cast<float>(text.length()) * scaleKnobMetric(5.6f));
}

float normaliseDisplayedKnobValue(const juce::Slider& slider)
{
    const auto paramId = slider.getProperties()["paramId"].toString();
    const auto value = static_cast<float>(slider.getValue());

    if (paramId == NComp::params::attack1 || paramId == NComp::params::attack2)
        return NComp::params::piecewiseNormalisedFromValue(NComp::params::kAttackLegendValues, value);

    if (paramId == NComp::params::release1 || paramId == NComp::params::release2)
        return NComp::params::piecewiseNormalisedFromValue(NComp::params::kReleaseLegendValues, value);

    const auto minValue = static_cast<float>(slider.getMinimum());
    const auto maxValue = static_cast<float>(slider.getMaximum());

    if (maxValue <= minValue)
        return 0.0f;

    return juce::jlimit(0.0f, 1.0f, (value - minValue) / (maxValue - minValue));
}

double parseSliderEntry(const juce::String& text, const juce::String& suffix, double fallbackValue, double minValue, double maxValue)
{
    auto cleaned = text.trim().replaceCharacter(',', '.');
    const bool useThousands = suffix.equalsIgnoreCase("ms") && cleaned.endsWithIgnoreCase("k");
    const auto numericText = cleaned.retainCharacters("0123456789.-");

    if (numericText.isEmpty() || numericText == "-" || numericText == "." || numericText == "-.")
        return fallbackValue;

    auto parsedValue = numericText.getDoubleValue();
    if (useThousands)
        parsedValue *= 1000.0;

    return juce::jlimit(minValue, maxValue, normaliseKnobValue(parsedValue, suffix));
}

void fillTexturedPanel(juce::Graphics& g,
                       juce::Rectangle<float> area,
                       float cornerSize,
                       juce::Colour top,
                       juce::Colour bottom,
                       float horizontalAlpha,
                       float verticalAlpha);

void drawPanelOutline(juce::Graphics& g, juce::Rectangle<float> area, float cornerSize);

void drawShadowedText(juce::Graphics& g,
                      const juce::String& text,
                      juce::Rectangle<float> area,
                      juce::Justification justification,
                      const juce::Font& font,
                      juce::Colour colour,
                      int maxLines = 1)
{
    g.setFont(font);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawFittedText(text, area.translated(0.0f, 1.0f).toNearestInt(), justification, maxLines);
    g.setColour(colour);
    g.drawFittedText(text, area.toNearestInt(), justification, maxLines);
}

void drawKnobCaption(juce::Graphics& g, juce::Slider& slider)
{
    const auto labelColour = juce::Colour::fromRGB(238, 236, 228).withAlpha(0.9f);
    const auto valueTop = juce::Colour::fromRGB(77, 72, 66);
    const auto valueBottom = juce::Colour::fromRGB(24, 23, 22);
    const juce::String title = slider.getName().toUpperCase();
    const auto labelArea = knobCaptionArea(slider);
    const bool isDragging = slider.isMouseButtonDown();

    if (!isDragging)
    {
        drawShadowedText(g, title, labelArea, juce::Justification::centred, makeFont(scaleKnobMetric(10.5f), juce::Font::bold), labelColour);
        return;
    }

    const auto valueText = slider.getTextFromValue(slider.getValue());
    const float boxWidth = juce::jmax(knobValueMinWidth,
                                      scaleKnobMetric(18.0f) + static_cast<float>(valueText.length()) * scaleKnobMetric(5.8f));
    auto valueArea = knobValueArea(slider, boxWidth);

    fillTexturedPanel(g, valueArea, 4.0f, valueTop, valueBottom, 0.02f, 0.012f);
    drawPanelOutline(g, valueArea, 4.0f);
    drawShadowedText(g, valueText,
                     valueArea,
                     juce::Justification::centred,
                     makeFont(scaleKnobMetric(10.5f), juce::Font::bold),
                     juce::Colour::fromRGB(249, 246, 240));
}

void fillTexturedPanel(juce::Graphics& g,
                       juce::Rectangle<float> area,
                       float cornerSize,
                       juce::Colour top,
                       juce::Colour bottom,
                       float horizontalAlpha,
                       float verticalAlpha)
{
    juce::ignoreUnused(horizontalAlpha, verticalAlpha);

    const auto liftedTop = lightenDarkSurface(top);
    const auto liftedBottom = lightenDarkSurface(bottom);
    const auto fill = liftedTop.interpolatedWith(liftedBottom, 0.5f);
    g.setColour(fill);

    if (cornerSize > 0.0f)
        g.fillRoundedRectangle(area, cornerSize);
    else
        g.fillRect(area);

    g.setColour(liftedTop.brighter(0.14f).withAlpha(0.22f));
    g.drawLine(area.getX() + 1.0f, area.getY() + 1.0f, area.getRight() - 1.0f, area.getY() + 1.0f, 1.0f);
}

void drawPanelOutline(juce::Graphics& g, juce::Rectangle<float> area, float cornerSize)
{
    const auto outer = juce::Colour::fromRGB(13, 16, 20);
    const auto inner = juce::Colour::fromRGB(77, 83, 92).withAlpha(0.25f);

    if (cornerSize > 0.0f)
    {
        g.setColour(outer);
        g.drawRoundedRectangle(area, cornerSize, 1.0f);
        g.setColour(inner);
        g.drawRoundedRectangle(area.reduced(1.0f), juce::jmax(0.0f, cornerSize - 1.0f), 1.0f);
    }
    else
    {
        g.setColour(outer);
        g.drawRect(area.toNearestInt(), 1);
        g.setColour(inner);
        g.drawRect(area.reduced(1.0f).toNearestInt(), 1);
    }
}

void drawSoftButtonShadow(juce::Graphics& g, juce::Rectangle<float> area, float cornerSize)
{
    if (cornerSize > 0.0f)
    {
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.drawRoundedRectangle(area.expanded(0.7f), cornerSize + 0.7f, 1.0f);
        return;
    }

    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.drawRect(area.expanded(1.0f).toNearestInt(), 1);
}

void drawSoftButtonShadowEllipse(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.drawEllipse(area.expanded(0.7f), 1.0f);
}

template <size_t N>
void drawArcScale(juce::Graphics& g,
                  juce::Point<float> centre,
                  float startAngle,
                  float endAngle,
                  float tickInnerRadius,
                  float tickOuterRadius,
                  float labelRadius,
                  const std::array<const char*, N>& labels,
                  const std::array<float, N>& proportions,
                  float fontSize,
                  float lineThickness,
                  float minLabelWidth = 28.0f)
{
    const auto textColour = juce::Colour::fromRGB(236, 234, 226).withAlpha(0.9f);

    for (size_t i = 0; i < N; ++i)
    {
        const float proportion = juce::jlimit(0.0f, 1.0f, proportions[i]);
        const float angle = startAngle + proportion * (endAngle - startAngle);
        const auto tickStart = pointOnCircle(centre, angle, tickInnerRadius);
        const auto tickEnd = pointOnCircle(centre, angle, tickOuterRadius);
        const auto labelPoint = pointOnCircle(centre, angle, labelRadius);
        const juce::String label(labels[i]);
        const float labelWidth = juce::jmax(minLabelWidth, 8.0f + static_cast<float>(label.length()) * fontSize * 0.52f);
        auto labelArea = juce::Rectangle<float>(0.0f, 0.0f, labelWidth, fontSize + 6.0f).withCentre(labelPoint);

        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawLine({ tickStart.translated(0.0f, 1.0f), tickEnd.translated(0.0f, 1.0f) }, lineThickness + 0.4f);
        g.setColour(textColour);
        g.drawLine({ tickStart, tickEnd }, lineThickness);

        drawShadowedText(g, label, labelArea, juce::Justification::centred, makeFont(fontSize), textColour);
    }
}

template <size_t N>
void drawArcScale(juce::Graphics& g,
                  juce::Point<float> centre,
                  float startAngle,
                  float endAngle,
                  float tickInnerRadius,
                  float tickOuterRadius,
                  float labelRadius,
                  const std::array<const char*, N>& labels,
                  float fontSize,
                  float lineThickness,
                  float minLabelWidth = 28.0f)
{
    std::array<float, N> proportions {};

    for (size_t i = 0; i < N; ++i)
        proportions[i] = N == 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(N - 1);

    drawArcScale(g,
                 centre,
                 startAngle,
                 endAngle,
                 tickInnerRadius,
                 tickOuterRadius,
                 labelRadius,
                 labels,
                 proportions,
                 fontSize,
                 lineThickness,
                 minLabelWidth);
}

void drawMeterWindow(juce::Graphics& g, juce::Rectangle<float> area)
{
    fillTexturedPanel(g, area, 4.0f, juce::Colour::fromRGB(31, 35, 41), juce::Colour::fromRGB(31, 35, 41), 0.0f, 0.0f);
    drawPanelOutline(g, area, 4.0f);

    auto glass = meterGlassArea(area);
    g.setColour(lightenDarkSurface(juce::Colour::fromRGB(18, 21, 26)));
    g.fillRoundedRectangle(glass, 2.0f);
    g.setColour(juce::Colour::fromRGB(10, 12, 16));
    g.drawRoundedRectangle(glass, 2.0f, 1.0f);

    const auto centre = meterPivot(area);
    drawArcScale(g,
                 centre,
                 meterStartAngle,
                 meterEndAngle,
                 meterArcInnerRadius(area),
                 meterArcOuterRadius(area),
                 meterLabelRadius(area),
                 meterScale,
                 7.2f,
                 1.0f,
                 16.0f);
    drawShadowedText(g, "dB", { area.getCentreX() - 14.0f, area.getY() + 34.0f, 28.0f, 10.0f }, juce::Justification::centred, makeFont(7.8f), juce::Colour::fromRGB(237, 235, 226).withAlpha(0.9f));
    drawShadowedText(g, "COMPRESSION", { area.getCentreX() - 42.0f, area.getY() + 46.0f, 84.0f, 10.0f }, juce::Justification::centred, makeFont(6.8f), juce::Colour::fromRGB(237, 235, 226).withAlpha(0.84f));
}

void drawBackgroundArt(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto background = lightenDarkSurface(juce::Colour::fromRGB(16, 18, 22));
    const auto bodyColour = lightenDarkSurface(juce::Colour::fromRGB(23, 26, 31));
    const auto sectionColour = juce::Colour::fromRGB(31, 35, 41);
    const auto utilityColour = juce::Colour::fromRGB(29, 33, 38);
    const auto meterColour = juce::Colour::fromRGB(26, 30, 35);
    const auto toolbarColour = juce::Colour::fromRGB(22, 25, 29);
    const auto dividerColour = juce::Colour::fromRGB(71, 76, 86).withAlpha(0.45f);

    g.fillAll(background);

    auto full = bounds.toFloat();
    auto bodyPanel = full.withTrimmedTop(static_cast<float>(Layout::bodyPanelTop));
    g.setColour(bodyColour);
    g.fillRect(bodyPanel);
    g.setColour(juce::Colour::fromRGB(8, 10, 13));
    g.drawRect(bodyPanel.toNearestInt(), 1);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawHorizontalLine(juce::roundToInt(bodyPanel.getY()), bodyPanel.getX() + 1.0f, bodyPanel.getRight() - 1.0f);

    auto drawSectionPanel = [&](juce::Rectangle<float> area, juce::Colour fillColour)
    {
        fillTexturedPanel(g, area, 6.0f, fillColour, fillColour, 0.0f, 0.0f);
        drawPanelOutline(g, area, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.drawHorizontalLine(juce::roundToInt(area.getY() + 1.0f), area.getX() + 1.0f, area.getRight() - 1.0f);
    };

    const auto topChannelPanel = juce::Rectangle<float>(static_cast<float>(Layout::channelPanelX),
                                                        static_cast<float>(Layout::topChannelPanelY),
                                                        static_cast<float>(Layout::channelPanelW),
                                                        static_cast<float>(Layout::channelPanelH));
    const auto bottomChannelPanel = juce::Rectangle<float>(static_cast<float>(Layout::channelPanelX),
                                                           static_cast<float>(Layout::bottomChannelPanelY),
                                                           static_cast<float>(Layout::channelPanelW),
                                                           static_cast<float>(Layout::channelPanelH));
    const auto mixPanel = juce::Rectangle<float>(static_cast<float>(Layout::mixPanelX),
                                                 static_cast<float>(Layout::mixPanelY),
                                                 static_cast<float>(Layout::mixPanelW),
                                                 static_cast<float>(Layout::mixPanelH));

    for (auto panel : { topChannelPanel, bottomChannelPanel })
        drawSectionPanel(panel, sectionColour);

    drawSectionPanel(mixPanel, sectionColour);

    const auto topInput = knobCentre(Layout::in1X, Layout::in1Y, 0, 0);
    const auto topAttack = knobCentre(Layout::attack1X, Layout::attack1Y, 0, 0);
    const auto topRelease = knobCentre(Layout::release1X, Layout::release1Y, 0, 0);
    const auto topThresh = knobCentre(Layout::thresh1X, Layout::thresh1Y, 0, 0);
    const auto topRatio = knobCentre(Layout::ratio1X, Layout::ratio1Y, 0, 0);
    const auto topOutput = knobCentre(Layout::out1X, Layout::out1Y, 0, 0);
    const auto topCharacter = knobCentre(Layout::characterX, Layout::characterY, 0, 0);
    const auto topMix = knobCentre(Layout::mixX, Layout::mixY, 0, 0);

    const auto bottomInput = knobCentre(Layout::in2X, Layout::in2Y, 0, 0);
    const auto bottomAttack = knobCentre(Layout::attack2X, Layout::attack2Y, 0, 0);
    const auto bottomRelease = knobCentre(Layout::release2X, Layout::release2Y, 0, 0);
    const auto bottomThresh = knobCentre(Layout::thresh2X, Layout::thresh2Y, 0, 0);
    const auto bottomRatio = knobCentre(Layout::ratio2X, Layout::ratio2Y, 0, 0);
    const auto bottomOutput = knobCentre(Layout::out2X, Layout::out2Y, 0, 0);

    for (auto centre : { topInput, bottomInput, topOutput, bottomOutput })
        drawArcScale(g, centre, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusWide, bipolarScale, bipolarScaleProportions, scaleKnobMetric(12.0f), scaleKnobMetric(1.2f), knobLegendMinWidth);

    for (auto centre : { topAttack, bottomAttack })
        drawArcScale(g, centre, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusDense, attackScale, attackScaleProportions, scaleKnobMetric(10.5f), scaleKnobMetric(1.0f), knobLegendMinWidth);

    for (auto centre : { topRelease, bottomRelease })
        drawArcScale(g, centre, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusDense, releaseScale, releaseScaleProportions, scaleKnobMetric(10.5f), scaleKnobMetric(1.0f), knobLegendMinWidth);

    for (auto centre : { topThresh, bottomThresh })
        drawArcScale(g, centre, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusDense, thresholdScale, thresholdScaleProportions, scaleKnobMetric(11.0f), scaleKnobMetric(1.1f), knobLegendMinWidth);

    for (auto centre : { topRatio, bottomRatio })
        drawArcScale(g, centre, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusDense, ratioScale, ratioScaleProportions, scaleKnobMetric(10.5f), scaleKnobMetric(1.0f), knobLegendMinWidth);

    drawArcScale(g, topCharacter, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusDense, NComp::params::kCharacterLegendLabels, NComp::params::kCharacterLegendProportions, scaleKnobMetric(9.0f), scaleKnobMetric(0.9f), knobLegendMinWidth);
    drawArcScale(g, topMix, legendStartAngle, legendEndAngle, knobTickInnerRadius, knobTickOuterRadius, knobLabelRadiusMix, mixScale, mixScaleProportions, scaleKnobMetric(12.0f), scaleKnobMetric(1.1f), knobLegendMinWidth);

    auto utilityPanel = juce::Rectangle<float>(static_cast<float>(Layout::utilityPanelX),
                                               static_cast<float>(Layout::utilityPanelY),
                                               static_cast<float>(Layout::utilityPanelW),
                                               static_cast<float>(Layout::utilityPanelH));
    drawSectionPanel(utilityPanel, utilityColour);

    auto meterHousing = juce::Rectangle<float>(static_cast<float>(Layout::meterHousingX),
                                               static_cast<float>(Layout::meterHousingY),
                                               static_cast<float>(Layout::meterHousingW),
                                               static_cast<float>(Layout::meterHousingH));
    drawSectionPanel(meterHousing, meterColour);

    auto toolbarPanel = juce::Rectangle<float>(static_cast<float>(Layout::toolbarX),
                                               static_cast<float>(Layout::toolbarY),
                                               static_cast<float>(Layout::toolbarW),
                                               static_cast<float>(Layout::toolbarH));
    fillTexturedPanel(g, toolbarPanel, 4.0f, toolbarColour, toolbarColour, 0.0f, 0.0f);
    drawPanelOutline(g, toolbarPanel, 4.0f);

    drawMeterWindow(g, { static_cast<float>(Layout::meterWindowX), static_cast<float>(Layout::meterTopY), static_cast<float>(Layout::meterWindowW), static_cast<float>(Layout::meterWindowH) });
    drawMeterWindow(g, { static_cast<float>(Layout::meterWindowX), static_cast<float>(Layout::meterBottomY), static_cast<float>(Layout::meterWindowW), static_cast<float>(Layout::meterWindowH) });

}
}

void MeterNeedleComponent::setValue(float normalised, float fallSmoothing)
{
    const float clamped = juce::jlimit(0.0f, 1.0f, normalised);
    const float smoothing = clamped > value ? 0.48f : fallSmoothing;
    const float nextValue = value + (clamped - value) * smoothing;
    const float snappedValue = std::abs(clamped - nextValue) < 0.0008f ? clamped : nextValue;

    if (std::abs(value - snappedValue) > 0.0001f)
    {
        value = snappedValue;
        repaint();
    }
}

void MeterNeedleComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (area.isEmpty())
        return;

    const auto pivot = meterPivot(area);
    const float angle = meterStartAngle + value * (meterEndAngle - meterStartAngle);
    const auto base = pointOnCircle(pivot, angle, 7.0f);
    const float tipRadius = meterNeedleRadius(area);
    const auto tip = pointOnCircle(pivot, angle, tipRadius);
    const auto glass = meterGlassArea(area);
    const auto needleColour = juce::Colour::fromRGB(208, 210, 211).withAlpha(0.9f);
    juce::Path needlePath;
    needlePath.startNewSubPath(base);
    needlePath.lineTo(tip);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(glass.toNearestInt());
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.strokePath(needlePath,
                 juce::PathStrokeType(3.0f,
                                      juce::PathStrokeType::JointStyle::mitered,
                                      juce::PathStrokeType::EndCapStyle::butt),
                 juce::AffineTransform::translation(0.0f, 1.0f));
    g.setColour(needleColour);
    g.strokePath(needlePath,
                 juce::PathStrokeType(2.1f,
                                      juce::PathStrokeType::JointStyle::mitered,
                                      juce::PathStrokeType::EndCapStyle::butt));
}

NCompLookAndFeel::NCompLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
}

void NCompLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    juce::ignoreUnused(sliderPos);

    const float displayProportion = normaliseDisplayedKnobValue(slider);
    const float angle = rotaryStartAngle + displayProportion * (rotaryEndAngle - rotaryStartAngle);
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(5.0f);

    if (bounds.isEmpty())
        return;

    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float trackRadius = radius * 0.80f;
    const float trackThickness = juce::jmax(2.5f, radius * 0.13f);
    const auto faceColour = lightenDarkSurface(juce::Colour::fromRGB(26, 29, 34));
    const auto outlineColour = juce::Colour::fromRGB(90, 96, 106).withAlpha(0.72f);
    const auto trackColour = juce::Colour::fromRGB(70, 76, 86);
    const auto markerColour = juce::Colour::fromRGB(245, 242, 236);

    g.setColour(faceColour);
    g.fillEllipse(bounds);
    g.setColour(outlineColour);
    g.drawEllipse(bounds, 1.2f);
    g.setColour(juce::Colour::fromRGB(11, 13, 16));
    g.drawEllipse(bounds.reduced(1.0f), 1.0f);

    juce::Path trackArc;
    trackArc.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(trackColour);
    g.strokePath(trackArc, juce::PathStrokeType(trackThickness, juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded));

    const auto markerStart = pointOnCircle(centre, angle, radius * 0.30f);
    const auto markerEnd = pointOnCircle(centre, angle, trackRadius + trackThickness * 0.25f);
    g.setColour(markerColour);
    g.drawLine({ markerStart, markerEnd }, 2.4f);

    auto capBounds = juce::Rectangle<float>(radius * 0.28f, radius * 0.28f).withCentre(centre);
    g.setColour(lightenDarkSurface(juce::Colour::fromRGB(14, 16, 20)));
    g.fillEllipse(capBounds);
    g.setColour(juce::Colour::fromRGB(72, 77, 86).withAlpha(0.55f));
    g.drawEllipse(capBounds, 1.0f);
}

void NCompLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                            juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.6f);

    if (bounds.isEmpty())
        return;

    const bool active = button.getToggleState();
    const bool minimalBackground = static_cast<bool>(button.getProperties()["minimalToolbarBackground"]);
    const float interaction = shouldDrawButtonAsDown ? 0.18f : (shouldDrawButtonAsHighlighted ? 0.08f : 0.0f);
    auto fill = active ? juce::Colour::fromRGB(228, 166, 70)
                       : (minimalBackground ? juce::Colours::transparentBlack
                                            : lightenDarkSurface(backgroundColour));
    fill = fill.interpolatedWith(juce::Colours::white, interaction * 0.12f);
    drawSoftButtonShadow(g, bounds, 4.0f);
    if (!fill.isTransparent())
    {
        g.setColour(fill.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.55f));
        g.fillRoundedRectangle(bounds, 4.0f);
    }
    g.setColour(juce::Colour::fromRGB(13, 16, 20).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.55f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void NCompLookAndFeel::drawButtonText(juce::Graphics& g,
                                      juce::TextButton& button,
                                      bool shouldDrawButtonAsHighlighted,
                                      bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    const auto textColour = button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                       : juce::TextButton::textColourOffId);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(textColour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.55f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

juce::Font NCompLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    const auto text = button.getButtonText();

    if (text == "<" || text == ">")
        return makeFont(static_cast<float>(buttonHeight) * 0.64f, juce::Font::bold);

    if (text.length() == 1)
        return makeFont(static_cast<float>(buttonHeight) * 0.56f, juce::Font::bold);

    return makeFont(juce::jmin(11.6f, static_cast<float>(buttonHeight) * 0.42f), juce::Font::bold);
}

void NCompLookAndFeel::drawComboBox(juce::Graphics& g,
                                    int width,
                                    int height,
                                    bool isButtonDown,
                                    int buttonX,
                                    int buttonY,
                                    int buttonW,
                                    int buttonH,
                                    juce::ComboBox& box)
{
    juce::ignoreUnused(isButtonDown);

    auto bounds = juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height)).reduced(0.6f);

    if (bounds.isEmpty())
        return;

    const bool active = box.hasKeyboardFocus(true) || box.isPopupActive();
    auto fill = active ? lightenDarkSurface(juce::Colour::fromRGB(41, 46, 53))
                       : lightenDarkSurface(juce::Colour::fromRGB(31, 35, 41));
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);
    drawPanelOutline(g, bounds, 6.0f);

    auto arrowArea = juce::Rectangle<float>(static_cast<float>(buttonX), static_cast<float>(buttonY), static_cast<float>(buttonW), static_cast<float>(buttonH)).reduced(7.0f, 8.5f);
    juce::Path arrow;
    arrow.startNewSubPath(arrowArea.getX(), arrowArea.getY());
    arrow.lineTo(arrowArea.getRight(), arrowArea.getY());
    arrow.lineTo(arrowArea.getCentreX(), arrowArea.getBottom());
    arrow.closeSubPath();

    g.setColour(active ? juce::Colour::fromRGB(241, 188, 97)
                       : box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

juce::Font NCompLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return makeFont(12.2f, juce::Font::bold);
}

void NCompLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    const int arrowSectionWidth = box.getHeight() + 8;
    label.setBounds(12, 1, juce::jmax(0, box.getWidth() - arrowSectionWidth - 10), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

NCompButton::NCompButton(const juce::String& name, Style buttonStyle, juce::String buttonPrimaryText, juce::String buttonSecondaryText)
    : juce::Button(name),
      style(buttonStyle),
      primaryText(std::move(buttonPrimaryText)),
      secondaryText(std::move(buttonSecondaryText))
{
    setClickingTogglesState(style != Style::state);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void NCompButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.75f);

    if (bounds.isEmpty())
        return;

    const bool active = getToggleState();
    const float interaction = shouldDrawButtonAsDown ? 0.18f : (shouldDrawButtonAsHighlighted ? 0.1f : 0.0f);

    const auto border = juce::Colour::fromRGB(13, 16, 20);
    const auto graphite = lightenDarkSurface(juce::Colour::fromRGB(35, 39, 45)).interpolatedWith(juce::Colours::white, interaction * 0.08f);
    const auto accent = juce::Colour::fromRGB(228, 166, 70).interpolatedWith(juce::Colours::white, interaction * 0.08f);
    const auto textOff = juce::Colour::fromRGB(229, 231, 234);
    const auto textOn = juce::Colour::fromRGB(26, 24, 20);

    auto drawPanel = [&](juce::Rectangle<float> area, float cornerSize, juce::Colour fill)
    {
        drawSoftButtonShadow(g, area, cornerSize);
        g.setColour(fill);
        g.fillRoundedRectangle(area, cornerSize);
        g.setColour(border);
        g.drawRoundedRectangle(area, cornerSize, 1.0f);
    };

    switch (style)
    {
        case Style::toggle:
        {
            drawPanel(bounds, 4.0f, active ? accent : graphite);

            auto content = bounds.reduced(4.0f, 3.0f);
            auto indicator = content.removeFromRight(6.0f).withSizeKeepingCentre(4.0f, 13.0f);
            const float fontHeight = primaryText.length() > 4 ? 9.0f : 10.5f;

            g.setColour(active ? juce::Colour::fromRGB(28, 24, 20) : juce::Colour::fromRGB(94, 99, 108));
            g.fillRoundedRectangle(indicator, 2.0f);

            g.setColour(active ? textOn : textOff);
            g.setFont(juce::Font(juce::FontOptions(fontHeight, juce::Font::bold)));
            g.drawFittedText(primaryText, content.toNearestInt(), juce::Justification::centred, 1);
            break;
        }

        case Style::mode:
        {
            drawPanel(bounds, 5.0f, graphite);

            auto segments = bounds.reduced(4.0f);
            auto left = segments.removeFromLeft(segments.getWidth() * 0.5f).reduced(1.0f);
            auto right = segments.reduced(1.0f);

            g.setColour(graphite.brighter(0.04f));
            g.fillRoundedRectangle(left, 7.0f);
            g.setColour(graphite.brighter(0.04f));
            g.fillRoundedRectangle(right, 7.0f);

            const auto activeSegment = active ? right : left;
            g.setColour(accent);
            g.fillRoundedRectangle(activeSegment, 7.0f);

            g.setColour(border.withAlpha(0.55f));
            g.drawLine(bounds.getCentreX(), bounds.getY() + 6.0f, bounds.getCentreX(), bounds.getBottom() - 6.0f, 1.0f);

            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.setColour(active ? textOff : textOn);
            g.drawFittedText(primaryText, left.toNearestInt(), juce::Justification::centred, 1);
            g.setColour(active ? textOn : textOff);
            g.drawFittedText(secondaryText, right.toNearestInt(), juce::Justification::centred, 1);
            break;
        }

        case Style::power:
        {
            auto buttonArea = bounds;
            juce::Rectangle<float> labelArea;

            if (primaryText.isNotEmpty())
                labelArea = buttonArea.removeFromBottom(15.0f);

            auto fill = active ? accent : juce::Colours::transparentBlack;
            fill = fill.interpolatedWith(juce::Colours::white, interaction * 0.12f);

            drawSoftButtonShadow(g, buttonArea, 4.0f);
            if (!fill.isTransparent())
            {
                g.setColour(fill);
                g.fillRoundedRectangle(buttonArea, 4.0f);
            }
            g.setColour(border);
            g.drawRoundedRectangle(buttonArea, 4.0f, 1.0f);

            const float glyphSize = juce::jmax(11.0f, juce::jmin(buttonArea.getWidth(), buttonArea.getHeight()) - (primaryText.isNotEmpty() ? 13.0f : 10.0f));
            auto glyphArea = juce::Rectangle<float>(glyphSize, glyphSize).withCentre(buttonArea.getCentre());
            const float glyphStroke = primaryText.isNotEmpty() ? 2.4f : 2.2f;

            g.setColour(active ? textOn : textOff.withAlpha(0.9f));
            g.drawEllipse(glyphArea, glyphStroke);
            g.drawLine(glyphArea.getCentreX(), glyphArea.getY() - 0.5f, glyphArea.getCentreX(), glyphArea.getCentreY(), glyphStroke);

            if (primaryText.isNotEmpty())
            {
                g.setColour(active ? juce::Colour::fromRGB(255, 214, 126) : textOff.withAlpha(0.75f));
                g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
                g.drawFittedText(primaryText, labelArea.toNearestInt(), juce::Justification::centred, 1);
            }
            break;
        }

        case Style::state:
        {
            drawSoftButtonShadowEllipse(g, bounds);
            g.setColour(active ? accent : graphite);
            g.fillEllipse(bounds);
            g.setColour(border);
            g.drawEllipse(bounds, 1.0f);

            g.setColour(active ? textOn : textOff);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.drawFittedText(primaryText, bounds.toNearestInt(), juce::Justification::centred, 1);
            break;
        }
    }
}

NCompAudioProcessorEditor::NCompAudioProcessorEditor(NCompAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(meterLeft);
    addAndMakeVisible(meterRight);

    addSlider(in1Slider, "Input", NComp::params::input1, "dB");
    addSlider(in2Slider, "Input", NComp::params::input2, "dB");
    addSlider(out1Slider, "Output", NComp::params::output1, "dB");
    addSlider(out2Slider, "Output", NComp::params::output2, "dB");

    addSlider(thresh1Slider, "Threshold", NComp::params::threshold1, "dB");
    addSlider(thresh2Slider, "Threshold", NComp::params::threshold2, "dB");

    addSlider(ratio1Slider, "Ratio", NComp::params::ratio1, "x");
    addSlider(ratio2Slider, "Ratio", NComp::params::ratio2, "x");

    addSlider(attack1Slider, "Attack", NComp::params::attack1, "ms");
    addSlider(attack2Slider, "Attack", NComp::params::attack2, "ms");

    addSlider(release1Slider, "Release", NComp::params::release1, "ms");
    addSlider(release2Slider, "Release", NComp::params::release2, "ms");

    addSlider(characterSlider, "Character", NComp::params::character, "");
    addSlider(mixSlider, "Mix", NComp::params::mix, "%");
    addCompactSlider(linkAmountSlider, linkAmountLabel, "Link %", NComp::params::linkAmount, "%");
    addCompactSlider(sidechainHpfSlider, sidechainHpfLabel, "SC HPF", NComp::params::sidechainHpf, "Hz");
    addCompactSlider(sidechainTiltSlider, sidechainTiltLabel, "SC Tilt", NComp::params::sidechainTilt, "dB");

    // Hidden combos keep parameter bindings alive but are not shown.
    addCombo(linkBox, "Link", NComp::params::link);
    addCombo(mslrBox, "L/R | M/S", NComp::params::mslr);
    addCombo(scBox, "Sidechain", NComp::params::sidechain);
    addCombo(listenBox, "SC Listen", NComp::params::sidechainListen);
    addCombo(powerBox, "Power", NComp::params::power);
    addCombo(autoGainBox, "Auto Gain", NComp::params::autoGain);
    linkBox.setVisible(false);
    mslrBox.setVisible(false);
    scBox.setVisible(false);
    listenBox.setVisible(false);
    powerBox.setVisible(false);
    autoGainBox.setVisible(false);

    configureToolbarButton(presetPrevButton);
    configureToolbarButton(presetNextButton);
    configureToolbarButton(presetOpenButton);
    configureToolbarButton(presetSaveButton);
    configureToolbarButton(abCopyButton);
    configureToolbarButton(abAButton);
    configureToolbarButton(abBButton);
    abAButton.setTriggeredOnMouseDown(true);
    abBButton.setTriggeredOnMouseDown(true);
    abAButton.getProperties().set("minimalToolbarBackground", true);
    abBButton.getProperties().set("minimalToolbarBackground", true);
    powerButton.setTooltip("Toggle processing on and off.");
    abCopyButton.setTooltip("Copy the active compare slot into the inactive slot.");
    abCopyButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(88, 58, 26));
    abCopyButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(232, 184, 98));
    abCopyButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(247, 242, 233));
    abCopyButton.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(43, 30, 15));

    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    presetLabel.setFont(makeFont(8.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(238, 236, 228).withAlpha(0.84f));
    presetLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(presetLabel);
    presetLabel.setVisible(false);

    presetBox.setTextWhenNothingSelected("Manual");
    presetBox.setJustificationType(juce::Justification::centredLeft);
    presetBox.setLookAndFeel(&lnf);
    presetBox.setColour(juce::ComboBox::backgroundColourId, lightenDarkSurface(juce::Colour::fromRGB(31, 28, 25)));
    presetBox.setColour(juce::ComboBox::buttonColourId, lightenDarkSurface(juce::Colour::fromRGB(24, 22, 20)));
    presetBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(15, 14, 13));
    presetBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(246, 242, 234));
    presetBox.setColour(juce::ComboBox::arrowColourId, juce::Colour::fromRGB(221, 172, 87));
    presetBox.onChange = [this]
    {
        handlePresetSelectionChange();
    };
    addAndMakeVisible(presetBox);

    rebuildPresetMenu();
    refreshPresetFileListAsync();

    presetPrevButton.onClick = [this]
    {
        stepPreset(-1);
    };

    presetNextButton.onClick = [this]
    {
        stepPreset(1);
    };

    presetOpenButton.onClick = [this]
    {
        openPresetFile();
    };

    presetSaveButton.onClick = [this]
    {
        savePreset(false);
    };

    abCopyButton.onClick = [this]
    {
        processor.copyActiveABSlotToInactive();
        syncButtonsFromState();
    };

    abAButton.onClick = [this]
    {
        if (abAButton.getToggleState())
            return;

        abBButton.setToggleState(false, juce::dontSendNotification);
        abAButton.setToggleState(true, juce::dontSendNotification);
        processor.selectABSlot(0);
        setABButtonVisualState(true);
    };

    abBButton.onClick = [this]
    {
        if (abBButton.getToggleState())
            return;

        abAButton.setToggleState(false, juce::dontSendNotification);
        abBButton.setToggleState(true, juce::dontSendNotification);
        processor.selectABSlot(1);
        setABButtonVisualState(false);
    };

    transferLabel.setText("IN --.-  OUT --.-  GR 0.0", juce::dontSendNotification);
    transferLabel.setJustificationType(juce::Justification::centredLeft);
    transferLabel.setFont(makeFont(8.0f, juce::Font::bold));
    transferLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(242, 238, 229).withAlpha(0.88f));
    transferLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(transferLabel);

    // Custom painted buttons
    addAndMakeVisible(linkButton);
    addAndMakeVisible(listenButton);
    addAndMakeVisible(scButton);
    addAndMakeVisible(autoGainButton);
    addAndMakeVisible(mslrButton);
    addAndMakeVisible(powerButton);
    autoGainButton.setTooltip("Match processed loudness to the input before output trim.");

    linkButton.onClick = [this]
    {
        const bool on = linkButton.getToggleState();
        setChoiceParam(NComp::params::link, on ? 0 : 1);
    };

    scButton.onClick = [this]
    {
        const bool on = scButton.getToggleState();
        setChoiceParam(NComp::params::sidechain, on ? 1 : 0);
    };

    listenButton.onClick = [this]
    {
        const bool on = listenButton.getToggleState();
        setChoiceParam(NComp::params::sidechainListen, on ? 1 : 0);
    };

    mslrButton.onClick = [this]
    {
        const bool ms = mslrButton.getToggleState();
        setChoiceParam(NComp::params::mslr, ms ? 1 : 0);
    };

    autoGainButton.onClick = [this]
    {
        const bool on = autoGainButton.getToggleState();
        setChoiceParam(NComp::params::autoGain, on ? 1 : 0);
    };

    powerButton.onClick = [this]
    {
        const bool on = powerButton.getToggleState();
        setChoiceParam(NComp::params::power, on ? 0 : 1);
    };

    addChildComponent(meterLabelL);
    addChildComponent(meterLabelR);

    for (auto* slider : getKnobSliders())
        slider->addMouseListener(this, false);

    for (auto* component : std::array<juce::Component*, 15>{ &meterLeft, &meterRight, &linkButton, &listenButton, &scButton, &autoGainButton, &mslrButton, &powerButton,
                                                              &presetPrevButton, &presetNextButton, &presetOpenButton, &presetSaveButton,
                                                              &abCopyButton, &abAButton, &abBButton })
        component->addMouseListener(this, false);

    knobValueEditor.setMultiLine(false);
    knobValueEditor.setReturnKeyStartsNewLine(false);
    knobValueEditor.setScrollbarsShown(false);
    knobValueEditor.setJustification(juce::Justification::centred);
    knobValueEditor.setIndents(0, 0);
    knobValueEditor.setBorder({ 1, 3, 1, 3 });
    knobValueEditor.setColour(juce::TextEditor::backgroundColourId, lightenDarkSurface(juce::Colour::fromRGB(44, 40, 36)));
    knobValueEditor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(249, 246, 240));
    knobValueEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour::fromRGB(189, 144, 72).withAlpha(0.6f));
    knobValueEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    knobValueEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::black.withAlpha(0.85f));
    knobValueEditor.setColour(juce::CaretComponent::caretColourId, juce::Colour::fromRGB(249, 246, 240));
    knobValueEditor.setFont(makeFont(10.5f, juce::Font::bold));
    knobValueEditor.onReturnKey = [this]
    {
        endKnobCaptionEdit(true);
    };
    knobValueEditor.onEscapeKey = [this]
    {
        endKnobCaptionEdit(false);
    };
    knobValueEditor.onFocusLost = [this]
    {
        endKnobCaptionEdit(true);
    };
    addChildComponent(knobValueEditor);

    syncButtonsFromState();
    syncPresetManager();

    startTimerHz(30);
    setSize(Layout::width, Layout::height);
}

NCompAudioProcessorEditor::~NCompAudioProcessorEditor()
{
    presetBox.setLookAndFeel(nullptr);
    for (auto* button : std::array<juce::TextButton*, 7>{ &presetPrevButton, &presetNextButton, &presetOpenButton, &presetSaveButton,
                                                          &abCopyButton, &abAButton, &abBButton })
        button->setLookAndFeel(nullptr);

    for (auto* slider : getKnobSliders())
        slider->removeMouseListener(this);

    for (auto* component : std::array<juce::Component*, 15>{ &meterLeft, &meterRight, &linkButton, &listenButton, &scButton, &autoGainButton, &mslrButton, &powerButton,
                                                              &presetPrevButton, &presetNextButton, &presetOpenButton, &presetSaveButton,
                                                              &abCopyButton, &abAButton, &abBButton })
        component->removeMouseListener(this);
}

void NCompAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackgroundArt(g, getLocalBounds());

    const auto getIdx = [&](const juce::String& id) -> int
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.getState().getParameter(id)))
            return choice->getIndex();
        return 0;
    };

    const bool msMode = getIdx(NComp::params::mslr) == 1;
    const auto sectionText = juce::Colour::fromRGB(232, 225, 210).withAlpha(0.78f);
    drawShadowedText(g,
                     msMode ? "MID CH" : "LEFT CH",
                     { static_cast<float>(Layout::channelPanelX + 14), static_cast<float>(Layout::topChannelPanelY + 6), 118.0f, 12.0f },
                     juce::Justification::centredLeft,
                     makeFont(8.3f, juce::Font::bold),
                     sectionText);
    drawShadowedText(g,
                     msMode ? "SIDE CH" : "RIGHT CH",
                     { static_cast<float>(Layout::channelPanelX + 14), static_cast<float>(Layout::bottomChannelPanelY + 6), 118.0f, 12.0f },
                     juce::Justification::centredLeft,
                     makeFont(8.3f, juce::Font::bold),
                     sectionText);
    drawShadowedText(g,
                     "MASTER",
                     { static_cast<float>(Layout::mixPanelX + 12), static_cast<float>(Layout::mixPanelY + 6), 66.0f, 12.0f },
                     juce::Justification::centredLeft,
                     makeFont(8.3f, juce::Font::bold),
                     sectionText);
    drawShadowedText(g,
                     "CONTROL",
                     { static_cast<float>(Layout::utilityPanelX + 12), static_cast<float>(Layout::utilityPanelY + 6), 78.0f, 12.0f },
                     juce::Justification::centredLeft,
                     makeFont(8.3f, juce::Font::bold),
                     sectionText);
        drawShadowedText(g,
                                         "PHANTOM-S",
                                         { static_cast<float>(Layout::meterHousingX + 16),
                                             static_cast<float>(Layout::meterTopY + Layout::meterWindowH + 12),
                                             static_cast<float>(Layout::meterHousingW - 32),
                                             14.0f },
                                         juce::Justification::centred,
                                         makeFont(10.5f, juce::Font::bold),
                                         juce::Colour::fromRGB(238, 236, 228).withAlpha(0.9f));

    for (auto* slider : getKnobSliders())
    {
        if (slider == editingSlider && knobValueEditor.isVisible())
            continue;

        drawKnobCaption(g, *slider);
    }
}

void NCompAudioProcessorEditor::resized()
{
    constexpr int ks = Layout::knobSize;
    constexpr int knobInset = (Layout::knobSlotSize - Layout::knobSize) / 2;
    auto placeKnob = [&](juce::Slider& slider, int baseX, int baseY, int offsetX, int offsetY)
    {
        slider.setBounds(baseX + offsetX + knobInset, baseY + offsetY + knobInset, ks, ks);
    };

    placeKnob(in1Slider, Layout::in1X, Layout::in1Y, 0, 0);
    placeKnob(attack1Slider, Layout::attack1X, Layout::attack1Y, 0, 0);
    placeKnob(release1Slider, Layout::release1X, Layout::release1Y, 0, 0);
    placeKnob(thresh1Slider, Layout::thresh1X, Layout::thresh1Y, 0, 0);
    placeKnob(ratio1Slider, Layout::ratio1X, Layout::ratio1Y, 0, 0);
    placeKnob(out1Slider, Layout::out1X, Layout::out1Y, 0, 0);
    placeKnob(characterSlider, Layout::characterX, Layout::characterY, 0, 0);
    placeKnob(mixSlider, Layout::mixX, Layout::mixY, 0, 0);

    placeKnob(in2Slider, Layout::in2X, Layout::in2Y, 0, 0);
    placeKnob(attack2Slider, Layout::attack2X, Layout::attack2Y, 0, 0);
    placeKnob(release2Slider, Layout::release2X, Layout::release2Y, 0, 0);
    placeKnob(thresh2Slider, Layout::thresh2X, Layout::thresh2Y, 0, 0);
    placeKnob(ratio2Slider, Layout::ratio2X, Layout::ratio2Y, 0, 0);
    placeKnob(out2Slider, Layout::out2X, Layout::out2Y, 0, 0);

    const int utilityInsetX = 8;
    const int utilityInsetY = 8;
    const int utilityGap = 3;
    const int utilityLabelWidth = Layout::utilityLabelW;
    const int utilityRowHeight = Layout::utilityRowH;
    const int utilitySliderWidth = Layout::utilityPanelW - utilityInsetX * 2 - utilityLabelWidth - 6;
    const int utilityLabelX = Layout::utilityPanelX + utilityInsetX;
    const int utilitySliderX = utilityLabelX + utilityLabelWidth + 6;

    const int toolbarButtonY = Layout::toolbarY + (Layout::toolbarH - Layout::toolbarButtonH) / 2;
    const int powerButtonY = Layout::toolbarY + (Layout::toolbarH - Layout::toolbarPowerH) / 2;
    const int powerButtonX = Layout::toolbarX + Layout::toolbarInsetX;
    const int compareGroupGap = Layout::toolbarSectionGap + 10;
    const int rightGroupWidth = Layout::toolbarActionW * 2
                              + compareGroupGap
                              + Layout::toolbarABW * 2
                              + Layout::toolbarCopyW
                              + Layout::toolbarGap * 3;
    const int rightGroupX = Layout::toolbarX + Layout::toolbarW - Layout::toolbarInsetX - rightGroupWidth;
    const int presetPrevX = powerButtonX + Layout::toolbarPowerW + Layout::toolbarSectionGap;
    const int presetBoxX = presetPrevX + Layout::toolbarArrowW + 4;
    const int presetNextX = rightGroupX - Layout::toolbarSectionGap - Layout::toolbarArrowW;
    const int presetBoxW = juce::jmax(220, presetNextX - 4 - presetBoxX);
    const int presetOpenX = rightGroupX;
    const int presetSaveX = presetOpenX + Layout::toolbarActionW + Layout::toolbarGap;
    const int abAButtonX = presetSaveX + Layout::toolbarActionW + compareGroupGap;
    const int abCopyButtonX = abAButtonX + Layout::toolbarABW + Layout::toolbarGap;
    const int abBButtonX = abCopyButtonX + Layout::toolbarCopyW + Layout::toolbarGap;

    powerButton.setBounds(powerButtonX, powerButtonY, Layout::toolbarPowerW, Layout::toolbarPowerH);
    presetPrevButton.setBounds(presetPrevX, toolbarButtonY, Layout::toolbarArrowW, Layout::toolbarButtonH);
    presetBox.setBounds(presetBoxX, toolbarButtonY, presetBoxW, Layout::toolbarButtonH);
    presetNextButton.setBounds(presetNextX, toolbarButtonY, Layout::toolbarArrowW, Layout::toolbarButtonH);
    presetOpenButton.setBounds(presetOpenX, toolbarButtonY, Layout::toolbarActionW, Layout::toolbarButtonH);
    presetSaveButton.setBounds(presetSaveX, toolbarButtonY, Layout::toolbarActionW, Layout::toolbarButtonH);
    abCopyButton.setBounds(abCopyButtonX, toolbarButtonY, Layout::toolbarCopyW, Layout::toolbarButtonH);
    abAButton.setBounds(abAButtonX, toolbarButtonY, Layout::toolbarABW, Layout::toolbarButtonH);
    abBButton.setBounds(abBButtonX, toolbarButtonY, Layout::toolbarABW, Layout::toolbarButtonH);

    const int utilityInnerBottom = Layout::utilityPanelY + Layout::utilityPanelH - 12;
    const int modeY = utilityInnerBottom - Layout::mslrH;
    const int bottomButtonRowY = modeY - Layout::buttonGap - Layout::toggleH;
    const int topButtonRowY = bottomButtonRowY - Layout::buttonGap - Layout::toggleH;
    const int utilityControlsHeight = Layout::utilityTransferH + 2 + 6 + utilityRowHeight * 3 + utilityGap * 2 + 2;
    const int utilityControlsTop = juce::jmax(Layout::utilityPanelY + 34,
                                              topButtonRowY - utilityControlsHeight - 8);
    const int utilityButtonW = (Layout::mslrW - Layout::buttonGap) / 2;
    const int utilityRightButtonX = Layout::mslrX + utilityButtonW + Layout::buttonGap;

    transferLabel.setBounds(Layout::utilityPanelX + utilityInsetX,
                            utilityControlsTop,
                            Layout::utilityPanelW - utilityInsetX * 2,
                            Layout::utilityTransferH + 2);

    auto placeUtilityRow = [&](juce::Label& label, juce::Slider& slider, int rowIndex)
    {
        const int y = utilityControlsTop + Layout::utilityTransferH + 6
                    + rowIndex * (utilityRowHeight + utilityGap);
        label.setBounds(utilityLabelX, y - 1, utilityLabelWidth, utilityRowHeight + 2);
        slider.setBounds(utilitySliderX, y + 1, utilitySliderWidth, utilityRowHeight - 2);
    };

    placeUtilityRow(linkAmountLabel, linkAmountSlider, 0);
    placeUtilityRow(sidechainHpfLabel, sidechainHpfSlider, 1);
    placeUtilityRow(sidechainTiltLabel, sidechainTiltSlider, 2);

    linkButton.setBounds(Layout::mslrX, topButtonRowY, utilityButtonW, Layout::toggleH);
    listenButton.setBounds(utilityRightButtonX, topButtonRowY, utilityButtonW, Layout::toggleH);
    scButton.setBounds(Layout::mslrX, bottomButtonRowY, utilityButtonW, Layout::toggleH);
    autoGainButton.setBounds(utilityRightButtonX, bottomButtonRowY, utilityButtonW, Layout::toggleH);
    mslrButton.setBounds(Layout::mslrX, modeY, Layout::mslrW, Layout::mslrH);

    meterLeft.setBounds(Layout::meterWindowX, Layout::meterTopY, Layout::meterWindowW, Layout::meterWindowH);
    meterRight.setBounds(Layout::meterWindowX, Layout::meterBottomY, Layout::meterWindowW, Layout::meterWindowH);

    if (editingSlider != nullptr && knobValueEditor.isVisible())
    {
        const auto currentText = knobValueEditor.getText();
        const float width = knobEditorWidth(currentText);
        knobValueEditor.setBounds(knobValueArea(*editingSlider, width).expanded(4.0f, 1.0f).toNearestInt());
    }
}

void NCompAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (editingSlider == nullptr || ! event.mods.isLeftButtonDown())
        return;

    if (event.originalComponent == &knobValueEditor || knobValueEditor.isParentOf(event.originalComponent))
        return;

    endKnobCaptionEdit(true);
}

void NCompAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (event.eventComponent == this && event.mods.isLeftButtonDown())
        if (auto* slider = findSliderForCaptionPoint(event.position))
        {
            beginKnobCaptionEdit(*slider);
            return;
        }

    juce::AudioProcessorEditor::mouseDoubleClick(event);
}

void NCompAudioProcessorEditor::timerCallback()
{
    const auto peakL = processor.getMeterL().load();
    const auto peakR = processor.getMeterR().load();
    const auto inputDb = processor.getTransferInputDb().load();
    const auto outputDb = processor.getTransferOutputDb().load();
    const auto reductionDb = processor.getTransferReductionDb().load();

    const bool powerOn = syncButtonsFromState();
    const float meterValueL = powerOn ? juce::jlimit(0.0f, 1.0f, peakL) : 0.0f;
    const float meterValueR = powerOn ? juce::jlimit(0.0f, 1.0f, peakR) : 0.0f;
    const float fallSmoothing = powerOn ? 0.18f : 0.09f;

    meterLeft.setValue(meterValueL, fallSmoothing);
    meterRight.setValue(meterValueR, fallSmoothing);

    transferLabel.setText(juce::String::formatted("IN %5.1f  OUT %5.1f  GR %4.1f", inputDb, outputDb, reductionDb),
                          juce::dontSendNotification);
    syncPresetManager();
}

void NCompAudioProcessorEditor::addSlider(juce::Slider& slider, const juce::String& name, const juce::String& paramId, const juce::String& suffix)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters(legendStartAngle, legendEndAngle, true);
    slider.setLookAndFeel(&lnf);
    addAndMakeVisible(slider);
    sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.getState(), paramId, slider));
    slider.setPopupDisplayEnabled(false, false, nullptr);
    if (paramId == NComp::params::character)
    {
        slider.textFromValueFunction = [](double rawValue)
        {
            return NComp::params::characterNameFromValue(static_cast<float>(rawValue));
        };

        slider.valueFromTextFunction = [parameter = processor.getState().getParameter(paramId)](const juce::String& text)
        {
            const auto fallbackValue = parameter != nullptr
                ? static_cast<double>(parameter->convertFrom0to1(parameter->getValue()))
                : 0.0;
            return static_cast<double>(NComp::params::characterValueFromText(text, static_cast<float>(fallbackValue)));
        };
    }
    else if (auto* parameter = processor.getState().getParameter(paramId))
    {
        slider.textFromValueFunction = [suffix](double rawValue)
        {
            return formatKnobValue(rawValue, suffix, true);
        };

        const auto minValue = static_cast<double>(parameter->convertFrom0to1(0.0f));
        const auto maxValue = static_cast<double>(parameter->convertFrom0to1(1.0f));
        slider.valueFromTextFunction = [parameter, suffix, minValue, maxValue](const juce::String& text)
        {
            const auto fallbackValue = static_cast<double>(parameter->convertFrom0to1(parameter->getValue()));
            return parseSliderEntry(text, suffix, fallbackValue, minValue, maxValue);
        };
    }
    slider.onValueChange = [this]
    {
        repaint();
    };
    slider.onDragStart = [this]
    {
        repaint();
    };
    slider.onDragEnd = [this]
    {
        repaint();
    };
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(processor.getState().getParameter(paramId)))
        slider.setDoubleClickReturnValue(true, param->get());
    else
        slider.setDoubleClickReturnValue(false, 0.0);

    slider.getProperties().set("paramId", paramId);
    slider.getProperties().set("suffix", suffix);
}

void NCompAudioProcessorEditor::addCompactSlider(juce::Slider& slider,
                                                 juce::Label& label,
                                                 const juce::String& name,
                                                 const juce::String& paramId,
                                                 const juce::String& suffix)
{
    label.setText(name.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredRight);
    label.setFont(makeFont(9.2f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(238, 236, 228).withAlpha(0.88f));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);

    slider.setName(name);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour(juce::Slider::backgroundColourId, lightenDarkSurface(juce::Colour::fromRGB(32, 30, 28)));
    slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(221, 172, 87));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(246, 240, 226));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, lightenDarkSurface(juce::Colour::fromRGB(32, 30, 28)));
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(221, 172, 87));
    slider.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(slider);
    sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.getState(), paramId, slider));

    if (auto* parameter = processor.getState().getParameter(paramId))
    {
        slider.textFromValueFunction = [suffix](double rawValue)
        {
            return formatKnobValue(rawValue, suffix, true);
        };

        const auto minValue = static_cast<double>(parameter->convertFrom0to1(0.0f));
        const auto maxValue = static_cast<double>(parameter->convertFrom0to1(1.0f));
        slider.valueFromTextFunction = [parameter, suffix, minValue, maxValue](const juce::String& text)
        {
            const auto fallbackValue = static_cast<double>(parameter->convertFrom0to1(parameter->getValue()));
            return parseSliderEntry(text, suffix, fallbackValue, minValue, maxValue);
        };
    }
}

void NCompAudioProcessorEditor::addCombo(juce::ComboBox& box, const juce::String& name, const juce::String& paramId)
{
    box.setTextWhenNothingSelected(name);
    setChoiceItems(box, paramId);
    addAndMakeVisible(box);
    comboAttachments.emplace_back(std::make_unique<ComboAttachment>(processor.getState(), paramId, box));
}

void NCompAudioProcessorEditor::setChoiceItems(juce::ComboBox& box, const juce::String& paramId)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.getState().getParameter(paramId)))
    {
        box.clear(juce::dontSendNotification);
        const auto& names = choice->choices;
        for (int i = 0; i < names.size(); ++i)
            box.addItem(names[i], i + 1);
    }
}

void NCompAudioProcessorEditor::setChoiceParam(const juce::String& paramId, int index)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.getState().getParameter(paramId)))
    {
        const float norm = choice->convertTo0to1(index);
        choice->beginChangeGesture();
        choice->setValueNotifyingHost(norm);
        choice->endChangeGesture();
    }
}

void NCompAudioProcessorEditor::configureToolbarButton(juce::TextButton& button, bool toggledButton)
{
    button.setClickingTogglesState(toggledButton);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    button.setLookAndFeel(&lnf);
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(43, 39, 35));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(221, 172, 87));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(245, 241, 233));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(43, 30, 15));
    button.setTriggeredOnMouseDown(false);
    addAndMakeVisible(button);
}

void NCompAudioProcessorEditor::refreshPresetFileListAsync()
{
    const auto scanGeneration = presetScanGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto presetDirectory = processor.getUserPresetDirectory();
    juce::Component::SafePointer<NCompAudioProcessorEditor> safeThis(this);

    std::thread([safeThis, presetDirectory, scanGeneration]()
    {
        std::vector<juce::File> presetFiles;

        if (presetDirectory.exists())
        {
            auto files = presetDirectory.findChildFiles(juce::File::findFiles, true, "*.ncompreset");
            presetFiles.assign(files.begin(), files.end());

            std::sort(presetFiles.begin(), presetFiles.end(), [&presetDirectory] (const juce::File& lhs, const juce::File& rhs)
            {
                auto lhsRelative = lhs.getRelativePathFrom(presetDirectory).replaceCharacter('\\', '/');
                auto rhsRelative = rhs.getRelativePathFrom(presetDirectory).replaceCharacter('\\', '/');
                return lhsRelative.compareIgnoreCase(rhsRelative) < 0;
            });
        }

        juce::MessageManager::callAsync([safeThis, presetFiles = std::move(presetFiles), scanGeneration]() mutable
        {
            if (safeThis == nullptr)
                return;

            if (safeThis->presetScanGeneration.load(std::memory_order_relaxed) != scanGeneration)
                return;

            safeThis->applyUserPresetFiles(std::move(presetFiles));
        });
    }).detach();
}

void NCompAudioProcessorEditor::applyUserPresetFiles(std::vector<juce::File> files)
{
    userPresetFiles = std::move(files);
    rebuildPresetMenu();
}

void NCompAudioProcessorEditor::rebuildPresetMenu()
{
    const auto presetDirectory = processor.getUserPresetDirectory();
    const auto displayPresetName = [&presetDirectory] (const juce::File& file)
    {
        auto relativePath = file.getRelativePathFrom(presetDirectory);

        if (relativePath.isEmpty())
            relativePath = file.getFileName();

        if (relativePath.endsWithIgnoreCase(".ncompreset"))
            relativePath = relativePath.dropLastCharacters(11);

        return relativePath.replaceCharacter('\\', '/');
    };

    if (processor.getCurrentPresetKind() == NCompAudioProcessor::PresetKind::user)
    {
        const auto currentFile = processor.getCurrentPresetFile();
        const bool alreadyListed = std::any_of(userPresetFiles.begin(), userPresetFiles.end(), [&currentFile](const juce::File& file)
        {
            return file == currentFile;
        });

        if (currentFile.getFullPathName().isNotEmpty() && !alreadyListed)
            userPresetFiles.push_back(currentFile);
    }

    std::sort(userPresetFiles.begin(), userPresetFiles.end(), [&displayPresetName] (const juce::File& lhs, const juce::File& rhs)
    {
        return displayPresetName(lhs).compareIgnoreCase(displayPresetName(rhs)) < 0;
    });

    syncingPresetBox = true;
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem("Manual", kManualPresetItemId);
    presetBox.addSeparator();
    presetBox.addSectionHeading("Factory Presets");

    for (int index = 1; index < processor.getNumPrograms(); ++index)
        presetBox.addItem(processor.getProgramName(index), kFactoryPresetItemBase + index);

    if (!userPresetFiles.empty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading("User Presets");

        for (size_t index = 0; index < userPresetFiles.size(); ++index)
            presetBox.addItem(displayPresetName(userPresetFiles[index]), kUserPresetItemBase + static_cast<int>(index));
    }

    syncingPresetBox = false;
    syncPresetManager();
}

void NCompAudioProcessorEditor::syncPresetManager()
{
    const auto presetKind = processor.getCurrentPresetKind();
    const bool dirty = processor.isCurrentPresetDirty();
    const auto presetName = processor.getCurrentPresetName();
    const int currentItemId = getCurrentPresetItemId();

    syncingPresetBox = true;
    presetBox.setTextWhenNothingSelected(presetName);
    if (currentItemId > 0)
        presetBox.setSelectedId(currentItemId, juce::dontSendNotification);
    syncingPresetBox = false;

    presetBox.setTooltip(dirty ? presetName + " *" : presetName);
    presetSaveButton.setEnabled(true);
    presetOpenButton.setEnabled(true);

        presetSaveButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(43, 39, 35));
        presetSaveButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(221, 172, 87));
}

void NCompAudioProcessorEditor::handlePresetSelectionChange()
{
    if (syncingPresetBox)
        return;

    const int selectedId = presetBox.getSelectedId();

    if (selectedId == kManualPresetItemId)
    {
        processor.markCurrentStateAsManual();
        syncPresetManager();
        return;
    }

    if (selectedId >= kFactoryPresetItemBase && selectedId < kUserPresetItemBase)
    {
        processor.setCurrentProgram(selectedId - kFactoryPresetItemBase);
        syncPresetManager();
        return;
    }

    if (selectedId >= kUserPresetItemBase)
    {
        const int userPresetIndex = selectedId - kUserPresetItemBase;

        if (juce::isPositiveAndBelow(userPresetIndex, static_cast<int>(userPresetFiles.size())))
        {
            juce::String errorMessage;
            if (!processor.loadPresetFromFile(userPresetFiles[static_cast<size_t>(userPresetIndex)], &errorMessage))
                showPresetMessage("Preset Load Failed", errorMessage);

            refreshPresetFileListAsync();
        }
    }
}

void NCompAudioProcessorEditor::stepPreset(int direction)
{
    std::vector<int> availableIds;

    for (int index = 1; index < processor.getNumPrograms(); ++index)
        availableIds.push_back(kFactoryPresetItemBase + index);

    for (size_t index = 0; index < userPresetFiles.size(); ++index)
        availableIds.push_back(kUserPresetItemBase + static_cast<int>(index));

    if (availableIds.empty())
        return;

    const int currentId = getCurrentPresetItemId();
    auto current = std::find(availableIds.begin(), availableIds.end(), currentId);

    if (direction > 0)
    {
        if (current == availableIds.end())
            presetBox.setSelectedId(availableIds.front(), juce::sendNotification);
        else
            presetBox.setSelectedId(*((current + 1) == availableIds.end() ? availableIds.begin() : (current + 1)), juce::sendNotification);
    }
    else
    {
        if (current == availableIds.end())
            presetBox.setSelectedId(availableIds.back(), juce::sendNotification);
        else if (current == availableIds.begin())
            presetBox.setSelectedId(availableIds.back(), juce::sendNotification);
        else
            presetBox.setSelectedId(*(current - 1), juce::sendNotification);
    }
}

void NCompAudioProcessorEditor::openPresetFile()
{
    auto startDirectory = processor.getCurrentPresetKind() == NCompAudioProcessor::PresetKind::user
        ? processor.getCurrentPresetFile().getParentDirectory()
        : processor.getUserPresetDirectory();

    activeFileChooser = std::make_unique<juce::FileChooser>("Open nComp preset", startDirectory, "*.ncompreset", true, false, this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this](const juce::FileChooser& chooser)
                                   {
                                       auto chosenFile = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (chosenFile == juce::File())
                                           return;

                                       juce::String errorMessage;
                                       if (!processor.loadPresetFromFile(chosenFile, &errorMessage))
                                       {
                                           showPresetMessage("Preset Load Failed", errorMessage);
                                           return;
                                       }

                                       refreshPresetFileListAsync();
                                   });
}

void NCompAudioProcessorEditor::savePreset(bool forceSaveAs)
{
    juce::File targetFile = processor.getCurrentPresetFile();

    if (forceSaveAs || processor.getCurrentPresetKind() != NCompAudioProcessor::PresetKind::user || targetFile == juce::File())
    {
        auto presetName = processor.getCurrentPresetName().trim();
        presetName = presetName.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_()[]");
        if (presetName.isEmpty())
            presetName = "nComp Preset";

        auto initialFile = processor.getUserPresetDirectory().getChildFile(presetName).withFileExtension(".ncompreset");
        activeFileChooser = std::make_unique<juce::FileChooser>("Save nComp preset", initialFile, "*.ncompreset", true, false, this);
        activeFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                           | juce::FileBrowserComponent::canSelectFiles
                                           | juce::FileBrowserComponent::warnAboutOverwriting,
                                       [this](const juce::FileChooser& chooser)
                                       {
                                           auto chosenFile = chooser.getResult();
                                           activeFileChooser.reset();

                                           if (chosenFile == juce::File())
                                               return;

                                           juce::String errorMessage;
                                           if (!processor.savePresetToFile(chosenFile, &errorMessage))
                                           {
                                               showPresetMessage("Preset Save Failed", errorMessage);
                                               return;
                                           }

                                           refreshPresetFileListAsync();
                                       });
        return;
    }

    juce::String errorMessage;
    if (!processor.savePresetToFile(targetFile, &errorMessage))
    {
        showPresetMessage("Preset Save Failed", errorMessage);
        return;
    }

    refreshPresetFileListAsync();
}

void NCompAudioProcessorEditor::showPresetMessage(const juce::String& title, const juce::String& message) const
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
}

int NCompAudioProcessorEditor::getCurrentPresetItemId() const
{
    switch (processor.getCurrentPresetKind())
    {
        case NCompAudioProcessor::PresetKind::factory:
            return kFactoryPresetItemBase + processor.getCurrentProgram();

        case NCompAudioProcessor::PresetKind::user:
        {
            const auto currentFile = processor.getCurrentPresetFile();
            for (size_t index = 0; index < userPresetFiles.size(); ++index)
                if (userPresetFiles[index] == currentFile)
                    return kUserPresetItemBase + static_cast<int>(index);

            return 0;
        }

        case NCompAudioProcessor::PresetKind::manual:
        default:
            return kManualPresetItemId;
    }
}

bool NCompAudioProcessorEditor::syncButtonsFromState()
{
    const auto getIdx = [&](const juce::String& id) -> int
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.getState().getParameter(id)))
            return choice->getIndex();
        return 0;
    };

    const int mslrModeIndex = getIdx(NComp::params::mslr);

    linkButton.setToggleState(getIdx(NComp::params::link) == 0, juce::dontSendNotification);
    listenButton.setToggleState(getIdx(NComp::params::sidechainListen) == 1, juce::dontSendNotification);
    scButton.setToggleState(getIdx(NComp::params::sidechain) == 1, juce::dontSendNotification);
    autoGainButton.setToggleState(getIdx(NComp::params::autoGain) == 1, juce::dontSendNotification);
    mslrButton.setToggleState(mslrModeIndex == 1, juce::dontSendNotification);
    const bool powerOn = getIdx(NComp::params::power) == 0;
    powerButton.setToggleState(powerOn, juce::dontSendNotification);
    const bool slotAActive = processor.getCurrentABSlot() == 0;
    setABButtonVisualState(slotAActive);

    if (lastMslrModeIndex != mslrModeIndex)
    {
        lastMslrModeIndex = mslrModeIndex;
        repaint();
    }

    return powerOn;
}

void NCompAudioProcessorEditor::setABButtonVisualState(bool slotAActive)
{
    abCopyButton.setButtonText(slotAActive ? ">" : "<");
    abCopyButton.setTooltip(slotAActive ? "Copy slot A settings to slot B." : "Copy slot B settings to slot A.");
    abAButton.setToggleState(slotAActive, juce::dontSendNotification);
    abBButton.setToggleState(!slotAActive, juce::dontSendNotification);
    abCopyButton.repaint();
    abAButton.repaint();
    abBButton.repaint();
}

std::array<juce::Slider*, 14> NCompAudioProcessorEditor::getKnobSliders()
{
    return { &in1Slider, &attack1Slider, &release1Slider, &thresh1Slider, &ratio1Slider, &out1Slider, &characterSlider, &mixSlider,
             &in2Slider, &attack2Slider, &release2Slider, &thresh2Slider, &ratio2Slider, &out2Slider };
}

std::array<const juce::Slider*, 14> NCompAudioProcessorEditor::getKnobSliders() const
{
    return { &in1Slider, &attack1Slider, &release1Slider, &thresh1Slider, &ratio1Slider, &out1Slider, &characterSlider, &mixSlider,
             &in2Slider, &attack2Slider, &release2Slider, &thresh2Slider, &ratio2Slider, &out2Slider };
}

juce::Slider* NCompAudioProcessorEditor::findSliderForCaptionPoint(juce::Point<float> point)
{
    for (auto* slider : getKnobSliders())
        if (knobCaptionArea(*slider).expanded(4.0f, 3.0f).contains(point))
            return slider;

    return nullptr;
}

void NCompAudioProcessorEditor::beginKnobCaptionEdit(juce::Slider& slider)
{
    if (editingSlider != nullptr && editingSlider != &slider)
        endKnobCaptionEdit(false);

    editingSlider = &slider;

    const auto currentText = formatKnobValue(slider.getValue(), slider.getProperties()["suffix"].toString(), false);
    const float width = knobEditorWidth(currentText);
    knobValueEditor.setText(currentText, juce::dontSendNotification);
    knobValueEditor.setBounds(knobValueArea(slider, width).expanded(4.0f, 1.0f).toNearestInt());
    knobValueEditor.setVisible(true);
    knobValueEditor.toFront(false);
    knobValueEditor.grabKeyboardFocus();
    knobValueEditor.selectAll();
    repaint();
}

void NCompAudioProcessorEditor::endKnobCaptionEdit(bool applyValue)
{
    auto* slider = editingSlider;
    if (slider == nullptr)
        return;

    const auto text = knobValueEditor.getText();
    editingSlider = nullptr;
    knobValueEditor.setVisible(false);

    if (applyValue)
        applyKnobCaptionText(*slider, text);

    repaint();
}

void NCompAudioProcessorEditor::applyKnobCaptionText(juce::Slider& slider, const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return;

    const auto nextValue = slider.valueFromTextFunction != nullptr ? slider.valueFromTextFunction(trimmed)
                                                                    : trimmed.getDoubleValue();
    const auto clampedValue = juce::jlimit(slider.getMinimum(), slider.getMaximum(), nextValue);

    if (auto* parameter = processor.getState().getParameter(slider.getProperties()["paramId"].toString()))
    {
        parameter->beginChangeGesture();
        slider.setValue(clampedValue, juce::sendNotificationSync);
        parameter->endChangeGesture();
    }
    else
    {
        slider.setValue(clampedValue, juce::sendNotificationSync);
    }
}
