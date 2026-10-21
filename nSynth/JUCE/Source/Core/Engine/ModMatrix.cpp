#include "ModMatrix.h"
#include <algorithm>
#include <cmath>

namespace nsynth
{
namespace
{
template <typename E>
bool inRange (E value, int count) noexcept
{
    const int index = (int) value;
    return index >= 0 && index < count;
}

float boundedAmount (float amount) noexcept
{
    return juce::jlimit (-1.0f, 1.0f, amount);
}
}

ModMatrix::ModMatrix()
{
    publish();
}

std::shared_ptr<const ModMatrixData> ModMatrix::snapshot() const noexcept
{
    return std::atomic_load_explicit (&published, std::memory_order_acquire);
}

float ModMatrix::getFixedAmount (ModSource source, ModTarget target) const noexcept
{
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, ModMatrixData::targetCount))
        return 0.0f;
    const auto current = snapshot();
    return current->fixed[(size_t) source][(size_t) target];
}

float ModMatrix::getDirectAmount (ModSource source, DirectModTarget target) const noexcept
{
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, ModMatrixData::directTargetCount))
        return 0.0f;
    const auto current = snapshot();
    return current->direct[(size_t) source][(size_t) target];
}

bool ModMatrix::hasSource (ModSource source) const noexcept
{
    if (! inRange (source, ModMatrixData::sourceCount))
        return false;
    const auto current = snapshot();
    const auto& fixed = current->fixed[(size_t) source];
    const auto& direct = current->direct[(size_t) source];
    return std::any_of (fixed.begin(), fixed.end(), [] (float x) { return x != 0.0f; })
        || std::any_of (direct.begin(), direct.end(), [] (float x) { return x != 0.0f; });
}

void ModMatrix::restoreSnapshot (const ModMatrixData& snapshotData)
{
    data = snapshotData;
    publish();
}

void ModMatrix::setFixedAmount (ModSource source, ModTarget target, float amount)
{
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, ModMatrixData::targetCount)
        || ! std::isfinite (amount))
        return;
    data.fixed[(size_t) source][(size_t) target] = boundedAmount (amount);
    publish();
}

bool ModMatrix::setDirectAmount (ModSource source, DirectModTarget target, float amount)
{
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, ModMatrixData::directTargetCount)
        || ! std::isfinite (amount) || isPanelDuplicate (source, target))
        return false;
    data.direct[(size_t) source][(size_t) target] = boundedAmount (amount);
    publish();
    return true;
}

void ModMatrix::clearAll()
{
    data = {};
    publish();
}

void ModMatrix::clearSource (ModSource source)
{
    if (! inRange (source, ModMatrixData::sourceCount))
        return;
    data.fixed[(size_t) source].fill (0.0f);
    data.direct[(size_t) source].fill (0.0f);
    publish();
}

void ModMatrix::clearTarget (ModTarget target)
{
    if (! inRange (target, ModMatrixData::targetCount))
        return;
    for (auto& row : data.fixed)
        row[(size_t) target] = 0.0f;
    publish();
}

void ModMatrix::clearDirectTarget (DirectModTarget target)
{
    if (! inRange (target, ModMatrixData::directTargetCount))
        return;
    for (auto& row : data.direct)
        row[(size_t) target] = 0.0f;
    publish();
}

juce::ValueTree ModMatrix::toValueTree (int layerIndex) const
{
    juce::ValueTree tree ("MOD_MATRIX");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);
    tree.setProperty ("layer", layerIndex, nullptr);

    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
    {
        for (int target = 0; target < ModMatrixData::targetCount; ++target)
        {
            const float amount = data.fixed[(size_t) source][(size_t) target];
            if (amount == 0.0f)
                continue;
            juce::ValueTree route ("ROUTE");
            route.setProperty ("source", source, nullptr);
            route.setProperty ("target", target, nullptr);
            route.setProperty ("amount", amount, nullptr);
            tree.addChild (route, -1, nullptr);
        }
        for (int target = 0; target < ModMatrixData::directTargetCount; ++target)
        {
            const float amount = data.direct[(size_t) source][(size_t) target];
            if (amount == 0.0f)
                continue;
            juce::ValueTree route ("DIRECT");
            route.setProperty ("source", source, nullptr);
            route.setProperty ("target", target, nullptr);
            route.setProperty ("amount", amount, nullptr);
            tree.addChild (route, -1, nullptr);
        }
    }
    return tree;
}

bool ModMatrix::restoreFromValueTree (const juce::ValueTree& tree, juce::String* error)
{
    const int version = (int) tree.getProperty ("schemaVersion", -1);
    if (! tree.hasType ("MOD_MATRIX") || (version != 1 && version != schemaVersion))
    {
        if (error) *error = "Invalid modulation matrix tree or unsupported schema version.";
        return false;
    }

    ModMatrixData candidate;
    std::array<std::array<bool, ModMatrixData::targetCount>, ModMatrixData::sourceCount> fixedSet {};
    std::array<std::array<bool, ModMatrixData::directTargetCount>, ModMatrixData::sourceCount> directSet {};
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto route = tree.getChild (i);
        const bool directRoute = route.hasType ("DIRECT");
        if (! directRoute && ! route.hasType ("ROUTE"))
        {
            if (error) *error = "Modulation matrix contains an unknown child node.";
            return false;
        }

        const int source = (int) route.getProperty ("source", -1);
        const int target = (int) route.getProperty ("target", -1);
        const float amount = (float) route.getProperty ("amount", 2.0f);
        const int targetCount = directRoute ? ModMatrixData::directTargetCount : ModMatrixData::targetCount;
        const int validSourceCount = version == 1 ? 8 : ModMatrixData::sourceCount;
        if (source < 0 || source >= validSourceCount || target < 0 || target >= targetCount
            || ! std::isfinite (amount) || amount < -1.0f || amount > 1.0f
            || (directRoute && isPanelDuplicate ((ModSource) source, (DirectModTarget) target)))
        {
            if (error) *error = "Modulation matrix contains an invalid route.";
            return false;
        }

        bool& occupied = directRoute ? directSet[(size_t) source][(size_t) target]
                                     : fixedSet[(size_t) source][(size_t) target];
        if (occupied)
        {
            if (error) *error = "Modulation matrix contains a duplicate route.";
            return false;
        }
        occupied = true;
        if (directRoute)
            candidate.direct[(size_t) source][(size_t) target] = amount;
        else
            candidate.fixed[(size_t) source][(size_t) target] = amount;
    }

    data = candidate;
    publish();
    return true;
}

bool ModMatrix::isPanelDuplicate (ModSource source, DirectModTarget target) noexcept
{
    // These source-to-amount paths are already hard-wired by the layer panel.
    return (source == ModSource::dds2 && (target == DirectModTarget::vcfDds2Amount || target == DirectModTarget::vcaDds2Amount))
        || (source == ModSource::lfo2 && (target == DirectModTarget::lfo2Rate || target == DirectModTarget::lfo2Delay))
        || (source == ModSource::env1 && (target == DirectModTarget::env1Attack
                                         || target == DirectModTarget::env1Sustain || target == DirectModTarget::env1Release));
}

const char* ModMatrix::getSourceName (ModSource source) noexcept
{
    static constexpr const char* names[] = { "DDS2", "LFO2", "ENV1", "Velocity", "Poly Aftertouch", "Expression",
                                              "Ribbon", "Note Number", "Per-note Pitch", "Timbre (CC74)" };
    return inRange (source, ModMatrixData::sourceCount) ? names[(int) source] : "Invalid";
}

const char* ModMatrix::getTargetName (ModTarget target) noexcept
{
    static constexpr const char* names[] = { "LFO1 Rate", "Cross Mod", "Wave Mod", "Oscillator Mix", "HPF Cutoff", "LPF Resonance", "ENV1 Decay", "Delay Time" };
    return inRange (target, ModMatrixData::targetCount) ? names[(int) target] : "Invalid";
}

const char* ModMatrix::getDirectTargetName (DirectModTarget target) noexcept
{
    static constexpr const char* names[] = {
        "DDS2 Tune", "LPF Cutoff", "VCF ENV Amount", "VCF LFO1 Amount", "VCF DDS2 Amount",
        "VCA ENV Level", "VCA LFO1 Amount", "VCA DDS2 Amount", "ENV1 Attack", "ENV1 Sustain",
        "ENV1 Release", "ENV2 Attack", "ENV2 Decay", "ENV2 Sustain", "ENV2 Release",
        "LFO1 Delay", "LFO1 LR Phase", "LFO2 Rate", "LFO2 Delay", "DDS Mod LFO1 Amount",
        "PW/Detune", "Portamento", "Delay Send", "Delay Feedback"
    };
    return inRange (target, ModMatrixData::directTargetCount) ? names[(int) target] : "Invalid";
}

void ModMatrix::publish()
{
    std::atomic_store_explicit (&published, std::make_shared<const ModMatrixData> (data), std::memory_order_release);
}
} // namespace nsynth
