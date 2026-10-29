#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <memory>
#include "LayerSnapshot.h"

namespace nsynth
{
// Modulation sources. The first ten keep their order (schema 1/2 routes refer to them by index).
enum class ModSource : int
{
    dds2, lfo2, env1, velocity, polyAftertouch, expression, ribbon, noteNumber, perNotePitch, timbre,
    env2, lfo1, mseg1, mseg2, modWheel, random,
    macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8,
    count
};

// Destinations of the former fixed matrix and direct mapping grids (schema 1/2 files and the
// convenience API below). Each one is a layer parameter, see legacyTargetSuffix().
enum class ModTarget : int
{
    lfo1Rate, crossMod, waveMod, oscillatorMix, hpfCutoff, lpfResonance, env1Decay, delayTime, count
};

enum class DirectModTarget : int
{
    dds2Tune, lpfCutoff, vcfEnvAmount, vcfLfo1Amount, vcfDds2Amount,
    vcaEnvLevel, vcaLfo1Amount, vcaDds2Amount, env1Attack, env1Sustain,
    env1Release, env2Attack, env2Decay, env2Sustain, env2Release,
    lfo1Delay, lfo1LrPhase, lfo2Rate, lfo2Delay, ddsModLfo1Amount,
    pwDetune, portamento, delayMix, delayFeedback, count
};

// One modulation route: source -> layer parameter, added to the parameter's normalised value.
struct ModSlot
{
    int source { -1 };        // ModSource, -1 = empty slot
    int destination { -1 };   // index in getLayerParamSpecs(), -1 = empty slot
    float amount { 0.0f };    // -1..1 of the destination's normalised range
    float curve { 0.0f };     // -1..1: bends the source response, 0 = linear
    bool bipolar { false };   // the 0..1 source swings -1..1 around the parameter value

    bool isActive() const noexcept { return source >= 0 && destination >= 0 && ! juce::exactlyEqual (amount, 0.0f); }
    bool isEmpty() const noexcept { return source < 0 || destination < 0; }
    bool operator== (const ModSlot& o) const noexcept
    {
        return source == o.source && destination == o.destination && juce::exactlyEqual (amount, o.amount)
            && juce::exactlyEqual (curve, o.curve) && bipolar == o.bipolar;
    }
    // Source value (0..1) after curve and polarity: the factor `amount` is multiplied with.
    float shape (float sourceValue) const noexcept;
};

// Multi-segment envelope / drawable LFO shape: points from x = 0 to x = 1, y 0..1. `curve` bends
// the segment that ends at the point (-1..1, 0 = straight line).
struct MsegShape
{
    static constexpr int kMaxPoints = 32;
    struct Point
    {
        float x { 0.0f }, y { 0.0f }, curve { 0.0f };
        bool operator== (const Point& o) const noexcept
        {
            return juce::exactlyEqual (x, o.x) && juce::exactlyEqual (y, o.y) && juce::exactlyEqual (curve, o.curve);
        }
    };

    std::array<Point, kMaxPoints> points {};
    int count { 0 };

    // Value at phase 0..1; `hint` caches the last segment (per voice) so evaluation is O(1).
    float evaluate (float phase, int& hint) const noexcept;
    static float bend (float t, float curve) noexcept;
    bool isValid() const noexcept;
    bool operator== (const MsegShape& o) const noexcept;

    enum class Preset { envelope, rampUp, rampDown, triangle, sine, square, steps };
    static MsegShape make (Preset preset);
    juce::String toString() const;
    static bool fromString (const juce::String& text, MsegShape& out);
};

// Per-layer patch data that is not a host parameter: modulation slots, macro names, MSEG shapes
// and the effect order. Published to the audio thread as an immutable snapshot.
struct ModMatrixData
{
    static constexpr int kMaxSlots = 64;
    static constexpr int kMacros = 8;
    static constexpr int kFxModules = (int) FxModule::count;
    static constexpr int sourceCount = (int) ModSource::count;

    ModMatrixData();

    std::array<ModSlot, kMaxSlots> slots {};
    std::array<std::array<char, 24>, kMacros> macroNames {};   // empty = "Macro N"
    std::array<MsegShape, 2> msegs {};
    std::array<FxModule, kFxModules> fxOrder {};

    // Derived when published: the active slots and what they touch (not compared, not saved).
    std::array<uint8_t, kMaxSlots> activeSlots {};
    int activeSlotCount { 0 };
    std::array<bool, kLayerParamCount> modulated {};
    std::array<bool, sourceCount> sourceUsed {};
    std::array<bool, kMaxSlots> slotOnLayer {};   // destination applied on the layer output
    bool anyVoiceDestination { false }, anyLayerDestination { false };

    bool sameContent (const ModMatrixData& other) const noexcept;
    void updateDerived() noexcept;
    static std::array<FxModule, kFxModules> defaultFxOrder() noexcept;
};

class ModMatrix
{
public:
    ModMatrix();

    std::shared_ptr<const ModMatrixData> snapshot() const noexcept;
    void restoreSnapshot (const ModMatrixData& snapshot);
    void clearAll();

    // ---- modulation slots -------------------------------------------------------------
    ModSlot getSlot (int index) const noexcept;
    // Adds a route, or updates the amount of an existing slot with the same source and
    // destination. Returns the slot index, -1 when the route is invalid or all slots are used.
    int addRoute (ModSource source, int destination, float amount, bool bipolar = false, float curve = 0.0f);
    bool setSlot (int index, const ModSlot& slot);
    void removeSlot (int index);
    void clearSource (ModSource source);
    void clearDestination (int destination);
    bool hasSource (ModSource source) const noexcept;
    int countRoutesTo (int destination) const noexcept;

    // ---- the former fixed / direct grids (schema 1/2 vocabulary) ---------------------------
    // A grid cell is a unipolar, linear slot of that source and destination.
    float getFixedAmount (ModSource source, ModTarget target) const noexcept;
    float getDirectAmount (ModSource source, DirectModTarget target) const noexcept;
    void setFixedAmount (ModSource source, ModTarget target, float amount);
    bool setDirectAmount (ModSource source, DirectModTarget target, float amount);
    void clearTarget (ModTarget target);
    void clearDirectTarget (DirectModTarget target);

    // ---- other patch data -------------------------------------------------------------------
    juce::String getMacroName (int macro) const;
    void setMacroName (int macro, const juce::String& name);
    MsegShape getMseg (int index) const noexcept;
    void setMseg (int index, const MsegShape& shape);
    std::array<FxModule, ModMatrixData::kFxModules> getFxOrder() const noexcept;
    // Moves the module at chain position `from` to position `to` (the others shift).
    void moveFxModule (int from, int to);
    bool setFxOrder (const std::array<FxModule, ModMatrixData::kFxModules>& order);

    juce::ValueTree toValueTree (int layerIndex) const;
    bool restoreFromValueTree (const juce::ValueTree& tree, juce::String* error = nullptr);

    static constexpr int schemaVersion = 3;
    static bool isPanelDuplicate (ModSource source, DirectModTarget target) noexcept;
    // Continuous layer parameters that respond to modulation (not macros or note-on-only settings).
    static bool isModulatable (int destination) noexcept;
    // Destinations applied once per block on the layer output (effects, pan) instead of per voice.
    static bool isLayerDestination (int destination) noexcept;
    static const char* getSourceName (ModSource source) noexcept;
    static const char* getSourceShortName (ModSource source) noexcept;
    static const char* getTargetName (ModTarget target) noexcept;
    static const char* getDirectTargetName (DirectModTarget target) noexcept;
    static const char* legacyTargetSuffix (ModTarget target) noexcept;
    static const char* legacyDirectSuffix (DirectModTarget target) noexcept;
    static const char* getFxModuleName (FxModule module) noexcept;

private:
    int findRoute (int source, int destination) const noexcept;   // unipolar linear slot
    void publish();
    ModMatrixData data;
    std::shared_ptr<const ModMatrixData> published;
};
} // namespace nsynth
