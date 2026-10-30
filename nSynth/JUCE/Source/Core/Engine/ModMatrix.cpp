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

int legacyDestination (ModTarget target) noexcept
{
    return findLayerSpecIndex (ModMatrix::legacyTargetSuffix (target));
}

int legacyDestination (DirectModTarget target) noexcept
{
    return findLayerSpecIndex (ModMatrix::legacyDirectSuffix (target));
}

const char* fxModuleIds[] = { "distortion", "eq", "compressor", "chorus", "phaser", "flanger", "delay", "reverb" };
} // namespace

//==============================================================================
float ModSlot::shape (float sourceValue) const noexcept
{
    float x = juce::jlimit (0.0f, 1.0f, sourceValue);
    if (! juce::exactlyEqual (curve, 0.0f))
        x = MsegShape::bend (x, curve);
    return bipolar ? 2.0f * x - 1.0f : x;
}

//==============================================================================
float MsegShape::bend (float t, float curve) noexcept
{
    // Rational tension curve: cheap (no exp/pow per sample), symmetric for +/- curve and exact at
    // both ends. curve > 0 rises fast and settles (convex), curve < 0 starts slowly (concave).
    const float a = juce::jlimit (-1.0f, 1.0f, curve) * 0.95f;
    const float w = (1.0f - a) / (1.0f + a);
    const float denominator = t + (1.0f - t) * w;
    return denominator > 0.0f ? t / denominator : 0.0f;
}

float MsegShape::evaluate (float phase, int& hint) const noexcept
{
    if (count < 2)
        return count == 1 ? points[0].y : 0.0f;
    phase = juce::jlimit (0.0f, 1.0f, phase);

    int i = juce::jlimit (0, count - 2, hint);
    if (phase < points[(size_t) i].x)
        i = 0;
    while (i < count - 2 && phase >= points[(size_t) i + 1].x)
        ++i;
    hint = i;

    const auto& a = points[(size_t) i];
    const auto& b = points[(size_t) i + 1];
    const float width = b.x - a.x;
    if (width <= 1.0e-6f)
        return b.y;
    const float t = juce::jlimit (0.0f, 1.0f, (phase - a.x) / width);
    return a.y + (b.y - a.y) * bend (t, b.curve);
}

bool MsegShape::isValid() const noexcept
{
    if (count < 2 || count > kMaxPoints || ! juce::exactlyEqual (points[0].x, 0.0f)
        || ! juce::exactlyEqual (points[(size_t) count - 1].x, 1.0f))
        return false;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = points[(size_t) i];
        if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.curve)
            || p.y < 0.0f || p.y > 1.0f || p.curve < -1.0f || p.curve > 1.0f
            || (i > 0 && p.x < points[(size_t) i - 1].x))
            return false;
    }
    return true;
}

bool MsegShape::operator== (const MsegShape& o) const noexcept
{
    if (count != o.count)
        return false;
    for (int i = 0; i < count; ++i)
        if (! (points[(size_t) i] == o.points[(size_t) i]))
            return false;
    return true;
}

MsegShape MsegShape::make (Preset preset)
{
    MsegShape s;
    auto add = [&s] (float x, float y, float curve = 0.0f) { s.points[(size_t) s.count++] = { x, y, curve }; };
    switch (preset)
    {
        case Preset::envelope:  add (0.0f, 0.0f); add (0.08f, 1.0f, 0.6f); add (0.35f, 0.55f, -0.6f); add (1.0f, 0.0f, -0.7f); break;
        case Preset::rampUp:    add (0.0f, 0.0f); add (1.0f, 1.0f); break;
        case Preset::rampDown:  add (0.0f, 1.0f); add (1.0f, 0.0f); break;
        case Preset::triangle:  add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
        case Preset::sine:
            add (0.0f, 0.5f); add (0.25f, 1.0f, 0.55f); add (0.5f, 0.5f, -0.55f);
            add (0.75f, 0.0f, 0.55f); add (1.0f, 0.5f, -0.55f);
            break;
        case Preset::square:    add (0.0f, 1.0f); add (0.5f, 1.0f); add (0.5f, 0.0f); add (1.0f, 0.0f); break;
        case Preset::steps:
        {
            static constexpr float levels[] = { 0.2f, 0.8f, 0.45f, 1.0f, 0.1f, 0.65f, 0.35f, 0.9f };
            add (0.0f, levels[0]);
            for (int k = 1; k < 8; ++k)
            {
                add ((float) k / 8.0f, levels[k - 1]);
                add ((float) k / 8.0f, levels[k]);
            }
            add (1.0f, levels[7]);
            break;
        }
    }
    return s;
}

juce::String MsegShape::toString() const
{
    juce::StringArray parts;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = points[(size_t) i];
        parts.add (juce::String (p.x, 5) + "," + juce::String (p.y, 5) + "," + juce::String (p.curve, 4));
    }
    return parts.joinIntoString (";");
}

bool MsegShape::fromString (const juce::String& text, MsegShape& out)
{
    const auto parts = juce::StringArray::fromTokens (text, ";", "");
    if (parts.size() < 2 || parts.size() > kMaxPoints)
        return false;
    MsegShape shape;
    for (const auto& part : parts)
    {
        const auto values = juce::StringArray::fromTokens (part, ",", "");
        if (values.size() != 3)
            return false;
        shape.points[(size_t) shape.count++] = { values[0].getFloatValue(), values[1].getFloatValue(), values[2].getFloatValue() };
    }
    if (! shape.isValid())
        return false;
    out = shape;
    return true;
}

//==============================================================================
ModMatrixData::ModMatrixData()
{
    msegs = { MsegShape::make (MsegShape::Preset::envelope), MsegShape::make (MsegShape::Preset::triangle) };
    fxOrder = defaultFxOrder();
    updateDerived();
}

std::array<FxModule, ModMatrixData::kFxModules> ModMatrixData::defaultFxOrder() noexcept
{
    // The version 1 chain (chorus -> delay -> reverb) with the new modules around it.
    return { FxModule::distortion, FxModule::eq, FxModule::compressor, FxModule::chorus,
             FxModule::phaser, FxModule::flanger, FxModule::delay, FxModule::reverb };
}

bool ModMatrixData::sameContent (const ModMatrixData& o) const noexcept
{
    return slots == o.slots && macroNames == o.macroNames && msegs == o.msegs && fxOrder == o.fxOrder;
}

void ModMatrixData::updateDerived() noexcept
{
    activeSlotCount = 0;
    modulated.fill (false);
    slotOnLayer.fill (false);
    sourceUsed.fill (false);
    anyVoiceDestination = anyLayerDestination = false;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const auto& slot = slots[(size_t) i];
        if (! slot.isActive())
            continue;
        activeSlots[(size_t) activeSlotCount++] = (uint8_t) i;
        modulated[(size_t) slot.destination] = true;
        sourceUsed[(size_t) slot.source] = true;
        slotOnLayer[(size_t) i] = ModMatrix::isLayerDestination (slot.destination);
        if (slotOnLayer[(size_t) i])
            anyLayerDestination = true;
        else
            anyVoiceDestination = true;
    }
}

//==============================================================================
ModMatrix::ModMatrix()
{
    publish();
}

std::shared_ptr<const ModMatrixData> ModMatrix::snapshot() const
{
    return handoff.latest();
}

void ModMatrix::restoreSnapshot (const ModMatrixData& snapshotData)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    data = snapshotData;
    publish();
}

void ModMatrix::clearAll()
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    data = {};
    publish();
}

ModSlot ModMatrix::getSlot (int index) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    return index >= 0 && index < ModMatrixData::kMaxSlots ? data.slots[(size_t) index] : ModSlot {};
}

int ModMatrix::addRoute (ModSource source, int destination, float amount, bool bipolar, float curve)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (! inRange (source, ModMatrixData::sourceCount) || ! isModulatable (destination) || ! std::isfinite (amount))
        return -1;
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        auto& slot = data.slots[(size_t) i];
        if (slot.source == (int) source && slot.destination == destination)
        {
            slot.amount = boundedAmount (amount);
            publish();
            return i;
        }
    }
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        auto& slot = data.slots[(size_t) i];
        if (slot.isEmpty())
        {
            slot = { (int) source, destination, boundedAmount (amount), juce::jlimit (-1.0f, 1.0f, curve), bipolar };
            publish();
            return i;
        }
    }
    return -1;
}

bool ModMatrix::setSlot (int index, const ModSlot& slot)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (index < 0 || index >= ModMatrixData::kMaxSlots)
        return false;
    // A slot may be half-filled while it is being edited (source chosen, destination not yet).
    const bool sourceOk = slot.source == -1 || inRange ((ModSource) slot.source, ModMatrixData::sourceCount);
    const bool destinationOk = slot.destination == -1 || isModulatable (slot.destination);
    if (! sourceOk || ! destinationOk || ! std::isfinite (slot.amount) || ! std::isfinite (slot.curve))
        return false;
    auto& target = data.slots[(size_t) index];
    target = slot;
    target.amount = boundedAmount (slot.amount);
    target.curve = juce::jlimit (-1.0f, 1.0f, slot.curve);
    publish();
    return true;
}

void ModMatrix::removeSlot (int index)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (index < 0 || index >= ModMatrixData::kMaxSlots)
        return;
    // Keep the used slots packed at the top, in their order.
    for (int i = index; i < ModMatrixData::kMaxSlots - 1; ++i)
        data.slots[(size_t) i] = data.slots[(size_t) i + 1];
    data.slots.back() = {};
    publish();
}

void ModMatrix::clearSource (ModSource source)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    for (int i = ModMatrixData::kMaxSlots - 1; i >= 0; --i)
        if (data.slots[(size_t) i].source == (int) source)
            for (int j = i; j < ModMatrixData::kMaxSlots; ++j)
                data.slots[(size_t) j] = j + 1 < ModMatrixData::kMaxSlots ? data.slots[(size_t) j + 1] : ModSlot {};
    publish();
}

void ModMatrix::clearDestination (int destination)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    for (int i = ModMatrixData::kMaxSlots - 1; i >= 0; --i)
        if (data.slots[(size_t) i].destination == destination)
            for (int j = i; j < ModMatrixData::kMaxSlots; ++j)
                data.slots[(size_t) j] = j + 1 < ModMatrixData::kMaxSlots ? data.slots[(size_t) j + 1] : ModSlot {};
    publish();
}

bool ModMatrix::hasSource (ModSource source) const noexcept
{
    return inRange (source, ModMatrixData::sourceCount) && snapshot()->sourceUsed[(size_t) source];
}

int ModMatrix::countRoutesTo (int destination) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    int n = 0;
    for (const auto& slot : data.slots)
        n += slot.isActive() && slot.destination == destination ? 1 : 0;
    return n;
}

int ModMatrix::findRoute (int source, int destination) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        const auto& slot = data.slots[(size_t) i];
        if (slot.source == source && slot.destination == destination && ! slot.bipolar && juce::exactlyEqual (slot.curve, 0.0f))
            return i;
    }
    return -1;
}

float ModMatrix::getFixedAmount (ModSource source, ModTarget target) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, (int) ModTarget::count))
        return 0.0f;
    const int index = findRoute ((int) source, legacyDestination (target));
    return index >= 0 ? data.slots[(size_t) index].amount : 0.0f;
}

float ModMatrix::getDirectAmount (ModSource source, DirectModTarget target) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, (int) DirectModTarget::count))
        return 0.0f;
    const int index = findRoute ((int) source, legacyDestination (target));
    return index >= 0 ? data.slots[(size_t) index].amount : 0.0f;
}

void ModMatrix::setFixedAmount (ModSource source, ModTarget target, float amount)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, (int) ModTarget::count) || ! std::isfinite (amount))
        return;
    const int destination = legacyDestination (target);
    const int index = findRoute ((int) source, destination);
    if (juce::exactlyEqual (amount, 0.0f))
    {
        if (index >= 0)
            removeSlot (index);
        return;
    }
    if (index >= 0)
    {
        data.slots[(size_t) index].amount = boundedAmount (amount);
        publish();
        return;
    }
    for (auto& slot : data.slots)
        if (slot.isEmpty())
        {
            slot = { (int) source, destination, boundedAmount (amount), 0.0f, false };
            publish();
            return;
        }
}

bool ModMatrix::setDirectAmount (ModSource source, DirectModTarget target, float amount)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (! inRange (source, ModMatrixData::sourceCount) || ! inRange (target, (int) DirectModTarget::count)
        || ! std::isfinite (amount) || isPanelDuplicate (source, target))
        return false;
    const int destination = legacyDestination (target);
    const int index = findRoute ((int) source, destination);
    if (juce::exactlyEqual (amount, 0.0f))
    {
        if (index >= 0)
            removeSlot (index);
        return true;
    }
    if (index >= 0)
    {
        data.slots[(size_t) index].amount = boundedAmount (amount);
        publish();
        return true;
    }
    for (auto& slot : data.slots)
        if (slot.isEmpty())
        {
            slot = { (int) source, destination, boundedAmount (amount), 0.0f, false };
            publish();
            return true;
        }
    return false;
}

void ModMatrix::clearTarget (ModTarget target)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (inRange (target, (int) ModTarget::count))
        clearDestination (legacyDestination (target));
}

void ModMatrix::clearDirectTarget (DirectModTarget target)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (inRange (target, (int) DirectModTarget::count))
        clearDestination (legacyDestination (target));
}

//==============================================================================
juce::String ModMatrix::getMacroName (int macro) const
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (macro < 0 || macro >= ModMatrixData::kMacros)
        return {};
    const juce::String name (data.macroNames[(size_t) macro].data());
    return name.isNotEmpty() ? name : "Macro " + juce::String (macro + 1);
}

void ModMatrix::setMacroName (int macro, const juce::String& name)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (macro < 0 || macro >= ModMatrixData::kMacros)
        return;
    auto& field = data.macroNames[(size_t) macro];
    field.fill (0);
    const auto trimmed = name.trim() == "Macro " + juce::String (macro + 1) ? juce::String() : name.trim();
    trimmed.copyToUTF8 (field.data(), field.size());
    publish();
}

MsegShape ModMatrix::getMseg (int index) const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    return index >= 0 && index < 2 ? data.msegs[(size_t) index] : MsegShape {};
}

void ModMatrix::setMseg (int index, const MsegShape& shape)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (index < 0 || index >= 2 || ! shape.isValid())
        return;
    data.msegs[(size_t) index] = shape;
    publish();
}

std::array<FxModule, ModMatrixData::kFxModules> ModMatrix::getFxOrder() const noexcept
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    return data.fxOrder;
}

void ModMatrix::moveFxModule (int from, int to)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    if (from < 0 || from >= ModMatrixData::kFxModules || to < 0 || to >= ModMatrixData::kFxModules || from == to)
        return;
    auto& order = data.fxOrder;
    const auto module = order[(size_t) from];
    if (from < to)
        std::rotate (order.begin() + from, order.begin() + from + 1, order.begin() + to + 1);
    else
        std::rotate (order.begin() + to, order.begin() + from, order.begin() + from + 1);
    jassert (order[(size_t) to] == module);
    juce::ignoreUnused (module);
    publish();
}

bool ModMatrix::setFxOrder (const std::array<FxModule, ModMatrixData::kFxModules>& order)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    std::array<bool, ModMatrixData::kFxModules> seen {};
    for (auto module : order)
    {
        if (! inRange (module, ModMatrixData::kFxModules) || seen[(size_t) module])
            return false;
        seen[(size_t) module] = true;
    }
    data.fxOrder = order;
    publish();
    return true;
}

//==============================================================================
juce::ValueTree ModMatrix::toValueTree (int layerIndex) const
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    juce::ValueTree tree ("MOD_MATRIX");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);
    tree.setProperty ("layer", layerIndex, nullptr);

    const auto& specs = getLayerParamSpecs();
    for (const auto& slot : data.slots)
    {
        if (slot.isEmpty())
            continue;
        juce::ValueTree node ("SLOT");
        node.setProperty ("source", slot.source, nullptr);
        node.setProperty ("dest", specs[(size_t) slot.destination].suffix, nullptr);
        node.setProperty ("amount", slot.amount, nullptr);
        if (! juce::exactlyEqual (slot.curve, 0.0f))
            node.setProperty ("curve", slot.curve, nullptr);
        if (slot.bipolar)
            node.setProperty ("bipolar", true, nullptr);
        tree.addChild (node, -1, nullptr);
    }
    for (int m = 0; m < ModMatrixData::kMacros; ++m)
        if (data.macroNames[(size_t) m][0] != 0)
        {
            juce::ValueTree node ("MACRO");
            node.setProperty ("index", m, nullptr);
            node.setProperty ("name", juce::String (data.macroNames[(size_t) m].data()), nullptr);
            tree.addChild (node, -1, nullptr);
        }
    for (int m = 0; m < 2; ++m)
    {
        juce::ValueTree node ("MSEG");
        node.setProperty ("index", m, nullptr);
        node.setProperty ("points", data.msegs[(size_t) m].toString(), nullptr);
        tree.addChild (node, -1, nullptr);
    }
    juce::StringArray order;
    for (auto module : data.fxOrder)
        order.add (fxModuleIds[(size_t) module]);
    juce::ValueTree chain ("FX_CHAIN");
    chain.setProperty ("order", order.joinIntoString (","), nullptr);
    tree.addChild (chain, -1, nullptr);
    return tree;
}

bool ModMatrix::restoreFromValueTree (const juce::ValueTree& tree, juce::String* error)
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    const int version = (int) tree.getProperty ("schemaVersion", -1);
    if (! tree.hasType ("MOD_MATRIX") || version < 1 || version > schemaVersion)
    {
        if (error) *error = "Invalid modulation matrix tree or unsupported schema version.";
        return false;
    }

    auto fail = [error] (const char* message)
    {
        if (error) *error = message;
        return false;
    };

    ModMatrixData candidate;
    int nextSlot = 0;
    auto addSlot = [&] (const ModSlot& slot)
    {
        if (nextSlot >= ModMatrixData::kMaxSlots)
            return false;
        candidate.slots[(size_t) nextSlot++] = slot;
        return true;
    };

    if (version < 3)
    {
        // Schema 1/2: a grid of fixed matrix targets (ROUTE) and direct mappings (DIRECT).
        std::array<std::array<bool, (size_t) ModTarget::count>, ModMatrixData::sourceCount> fixedSet {};
        std::array<std::array<bool, (size_t) DirectModTarget::count>, ModMatrixData::sourceCount> directSet {};
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto route = tree.getChild (i);
            const bool directRoute = route.hasType ("DIRECT");
            if (! directRoute && ! route.hasType ("ROUTE"))
                return fail ("Modulation matrix contains an unknown child node.");

            const int source = (int) route.getProperty ("source", -1);
            const int target = (int) route.getProperty ("target", -1);
            const float amount = (float) route.getProperty ("amount", 2.0f);
            const int targetCount = directRoute ? (int) DirectModTarget::count : (int) ModTarget::count;
            const int validSourceCount = version == 1 ? 8 : 10;
            if (source < 0 || source >= validSourceCount || target < 0 || target >= targetCount
                || ! std::isfinite (amount) || amount < -1.0f || amount > 1.0f
                || (directRoute && isPanelDuplicate ((ModSource) source, (DirectModTarget) target)))
                return fail ("Modulation matrix contains an invalid route.");

            bool& occupied = directRoute ? directSet[(size_t) source][(size_t) target] : fixedSet[(size_t) source][(size_t) target];
            if (occupied)
                return fail ("Modulation matrix contains a duplicate route.");
            occupied = true;
            if (juce::exactlyEqual (amount, 0.0f))
                continue;
            const int destination = directRoute ? legacyDestination ((DirectModTarget) target) : legacyDestination ((ModTarget) target);
            if (! addSlot ({ source, destination, amount, 0.0f, false }))
                return fail ("Modulation matrix contains more routes than there are slots.");
        }
    }
    else
    {
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto node = tree.getChild (i);
            if (node.hasType ("SLOT"))
            {
                const int source = (int) node.getProperty ("source", -1);
                const int destination = findLayerSpecIndex (node.getProperty ("dest").toString());
                const float amount = (float) node.getProperty ("amount", 2.0f);
                const float curve = (float) node.getProperty ("curve", 0.0f);
                if (source < 0 || source >= ModMatrixData::sourceCount || ! std::isfinite (amount) || amount < -1.0f || amount > 1.0f
                    || ! std::isfinite (curve) || curve < -1.0f || curve > 1.0f)
                    return fail ("Modulation matrix contains an invalid slot.");
                if (! isModulatable (destination))
                    continue; // a destination of a newer version: skipped
                if (! addSlot ({ source, destination, amount, curve, (bool) node.getProperty ("bipolar", false) }))
                    return fail ("Modulation matrix contains more slots than supported.");
            }
            else if (node.hasType ("MACRO"))
            {
                const int index = (int) node.getProperty ("index", -1);
                if (index < 0 || index >= ModMatrixData::kMacros)
                    return fail ("Modulation matrix contains an invalid macro name.");
                auto& field = candidate.macroNames[(size_t) index];
                field.fill (0);
                node.getProperty ("name").toString().copyToUTF8 (field.data(), field.size());
            }
            else if (node.hasType ("MSEG"))
            {
                const int index = (int) node.getProperty ("index", -1);
                if (index < 0 || index >= 2 || ! MsegShape::fromString (node.getProperty ("points").toString(), candidate.msegs[(size_t) index]))
                    return fail ("Modulation matrix contains an invalid MSEG shape.");
            }
            else if (node.hasType ("FX_CHAIN"))
            {
                const auto ids = juce::StringArray::fromTokens (node.getProperty ("order").toString(), ",", "");
                std::array<bool, ModMatrixData::kFxModules> seen {};
                if (ids.size() != ModMatrixData::kFxModules)
                    return fail ("Modulation matrix contains an invalid effect order.");
                for (int k = 0; k < ids.size(); ++k)
                {
                    int module = -1;
                    for (int m = 0; m < ModMatrixData::kFxModules; ++m)
                        if (ids[k] == fxModuleIds[m])
                            module = m;
                    if (module < 0 || seen[(size_t) module])
                        return fail ("Modulation matrix contains an invalid effect order.");
                    seen[(size_t) module] = true;
                    candidate.fxOrder[(size_t) k] = (FxModule) module;
                }
            }
            // Other children (written by a newer version) are ignored.
        }
    }

    data = candidate;
    publish();
    return true;
}

//==============================================================================
bool ModMatrix::isPanelDuplicate (ModSource source, DirectModTarget target) noexcept
{
    // These source-to-amount paths are already hard-wired by the layer panel.
    return (source == ModSource::dds2 && (target == DirectModTarget::vcfDds2Amount || target == DirectModTarget::vcaDds2Amount))
        || (source == ModSource::lfo2 && (target == DirectModTarget::lfo2Rate || target == DirectModTarget::lfo2Delay))
        || (source == ModSource::env1 && (target == DirectModTarget::env1Attack
                                         || target == DirectModTarget::env1Sustain || target == DirectModTarget::env1Release));
}

namespace
{
// Per-destination flags, built once: both checks run on the audio thread every block.
struct DestinationTables
{
    std::array<bool, kLayerParamCount> modulatable {}, layer {};

    DestinationTables()
    {
        const auto& specs = getLayerParamSpecs();
        for (size_t d = 0; d < specs.size() && d < kLayerParamCount; ++d)
        {
            const auto& spec = specs[d];
            const juce::String suffix (spec.suffix);
            // Read only when a note starts: modulating them would not be heard.
            modulatable[d] = spec.kind == Kind::floatParam && spec.section != Section::macros
                          && suffix != pid::stereoWidth && suffix != pid::velCurve;
            for (auto layerSection : { Section::fx, Section::delay, Section::reverb, Section::fxDistortion, Section::fxEq,
                                       Section::fxCompressor, Section::fxPhaser, Section::fxFlanger })
                layer[d] = layer[d] || spec.section == layerSection;
            layer[d] = layer[d] || suffix == pid::pan;
        }
    }
};

const DestinationTables& destinationTables()
{
    static const DestinationTables tables;
    return tables;
}
} // namespace

bool ModMatrix::isModulatable (int destination) noexcept
{
    return destination >= 0 && destination < (int) kLayerParamCount && destinationTables().modulatable[(size_t) destination];
}

bool ModMatrix::isLayerDestination (int destination) noexcept
{
    return destination >= 0 && destination < (int) kLayerParamCount && destinationTables().layer[(size_t) destination];
}

const char* ModMatrix::getSourceName (ModSource source) noexcept
{
    static constexpr const char* names[] = { "DDS2", "LFO2", "ENV1", "Velocity", "Poly Aftertouch", "Expression",
                                              "Ribbon", "Note Number", "Per-note Pitch", "Timbre (CC74)",
                                              "ENV2", "LFO1", "MSEG 1", "MSEG 2", "Mod Wheel", "Random",
                                              "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    static_assert (std::size (names) == (size_t) ModSource::count);
    return inRange (source, ModMatrixData::sourceCount) ? names[(int) source] : "Invalid";
}

const char* ModMatrix::getSourceShortName (ModSource source) noexcept
{
    static constexpr const char* names[] = { "DDS2", "LFO2", "ENV1", "VEL", "AT", "EXPR", "RIBBON", "NOTE", "PITCH", "TIMBRE",
                                              "ENV2", "LFO1", "MSEG1", "MSEG2", "WHEEL", "RAND",
                                              "M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8" };
    static_assert (std::size (names) == (size_t) ModSource::count);
    return inRange (source, ModMatrixData::sourceCount) ? names[(int) source] : "?";
}

const char* ModMatrix::getTargetName (ModTarget target) noexcept
{
    static constexpr const char* names[] = { "LFO1 Rate", "Cross Mod", "Wave Mod", "Oscillator Mix", "HPF Cutoff", "LPF Resonance", "ENV1 Decay", "Delay Time" };
    return inRange (target, (int) ModTarget::count) ? names[(int) target] : "Invalid";
}

const char* ModMatrix::getDirectTargetName (DirectModTarget target) noexcept
{
    static constexpr const char* names[] = {
        "DDS2 Tune", "LPF Cutoff", "VCF ENV Amount", "VCF LFO1 Amount", "VCF DDS2 Amount",
        "VCA ENV Level", "VCA LFO1 Amount", "VCA DDS2 Amount", "ENV1 Attack", "ENV1 Sustain",
        "ENV1 Release", "ENV2 Attack", "ENV2 Decay", "ENV2 Sustain", "ENV2 Release",
        "LFO1 Delay", "LFO1 LR Phase", "LFO2 Rate", "LFO2 Delay", "DDS Mod LFO1 Amount",
        "PW/Detune", "Portamento", "Delay Mix", "Delay Feedback"
    };
    return inRange (target, (int) DirectModTarget::count) ? names[(int) target] : "Invalid";
}

const char* ModMatrix::legacyTargetSuffix (ModTarget target) noexcept
{
    // DELAY TIME moves the division in SYNC mode and the milliseconds otherwise (see SynthLayer).
    static constexpr const char* suffixes[] = { pid::lfo1Rate, pid::crossMod, pid::pwmWave, pid::mix,
                                                pid::hpf, pid::res, pid::env1Decay, pid::delayMs };
    return inRange (target, (int) ModTarget::count) ? suffixes[(int) target] : "";
}

const char* ModMatrix::legacyDirectSuffix (DirectModTarget target) noexcept
{
    static constexpr const char* suffixes[] = {
        pid::dds2Tune, pid::lpf, pid::vcfEnv, pid::vcfLfo1, pid::vcfDds2,
        pid::vcaEnv, pid::vcaLfo1, pid::vcaDds2, pid::env1Attack, pid::env1Sustain,
        pid::env1Release, pid::env2Attack, pid::env2Decay, pid::env2Sustain, pid::env2Release,
        pid::lfo1Delay, pid::lfo1LrPhase, pid::lfo2Rate, pid::lfo2Delay, pid::modLfo1,
        pid::pwDetune, pid::portamento, pid::delayMix, pid::delayFeedback
    };
    return inRange (target, (int) DirectModTarget::count) ? suffixes[(int) target] : "";
}

const char* ModMatrix::getFxModuleName (FxModule module) noexcept
{
    static constexpr const char* names[] = { "Distortion", "EQ", "Compressor", "Chorus", "Phaser", "Flanger", "Delay", "Reverb" };
    return inRange (module, ModMatrixData::kFxModules) ? names[(int) module] : "Invalid";
}

void ModMatrix::publish()
{
    const std::lock_guard<std::recursive_mutex> edit (editLock);
    data.updateDerived();
    handoff.publish (std::make_shared<const ModMatrixData> (data));
}
} // namespace nsynth
