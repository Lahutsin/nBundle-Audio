#include "SequenceBank.h"

namespace nsynth
{
namespace
{
constexpr size_t kHeaderSize = SequenceBankData::kNumSlots;
constexpr size_t kStepSize = SequenceStep::kMaxNotes + 3;
constexpr size_t kPayloadSize = kHeaderSize
                              + (size_t) SequenceBankData::kNumSlots * Sequence::kMaxSteps * kStepSize;

bool validSlotAndStep (int slot, int step) noexcept
{
    return juce::isPositiveAndBelow (slot, SequenceBankData::kNumSlots)
        && juce::isPositiveAndBelow (step, Sequence::kMaxSteps);
}

bool validStep (const SequenceStep& step) noexcept
{
    if (step.noteCount > SequenceStep::kMaxNotes || step.gatePercent < 1 || step.gatePercent > 100)
        return false;
    for (int i = 0; i < (int) step.noteCount; ++i)
        if (step.notes[(size_t) i] < -48 || step.notes[(size_t) i] > 48)
            return false;
    return step.rest || step.noteCount > 0;
}

size_t stepOffset (int slot, int step) noexcept
{
    return kHeaderSize + ((size_t) slot * Sequence::kMaxSteps + (size_t) step) * kStepSize;
}
} // namespace

bool SequenceStep::operator== (const SequenceStep& other) const noexcept
{
    return notes == other.notes && noteCount == other.noteCount && slide == other.slide
        && accent == other.accent && rest == other.rest && gatePercent == other.gatePercent;
}

bool Sequence::operator== (const Sequence& other) const noexcept
{
    return length == other.length && steps == other.steps;
}

bool SequenceBankData::operator== (const SequenceBankData& other) const noexcept
{
    return slots == other.slots;
}

SequenceBank::SequenceBank()
    : data (std::make_shared<SequenceBankData>())
{
}

bool SequenceBank::setStep (int slot, int step, const SequenceStep& value)
{
    if (! validSlotAndStep (slot, step) || ! validStep (value))
        return false;
    auto updated = std::make_shared<SequenceBankData> (*snapshot());
    updated->slots[(size_t) slot].steps[(size_t) step] = value;
    std::atomic_store (&data, std::shared_ptr<const SequenceBankData> (std::move (updated)));
    return true;
}

bool SequenceBank::setLength (int slot, int length)
{
    if (! juce::isPositiveAndBelow (slot, SequenceBankData::kNumSlots) || length < 1 || length > Sequence::kMaxSteps)
        return false;
    auto updated = std::make_shared<SequenceBankData> (*snapshot());
    updated->slots[(size_t) slot].length = (uint8_t) length;
    std::atomic_store (&data, std::shared_ptr<const SequenceBankData> (std::move (updated)));
    return true;
}

bool SequenceBank::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, SequenceBankData::kNumSlots))
        return false;
    auto updated = std::make_shared<SequenceBankData> (*snapshot());
    updated->slots[(size_t) slot] = Sequence {};
    std::atomic_store (&data, std::shared_ptr<const SequenceBankData> (std::move (updated)));
    return true;
}

void SequenceBank::clearAll()
{
    std::atomic_store (&data, std::make_shared<const SequenceBankData>());
}

void SequenceBank::restoreSnapshot (const SequenceBankData& snapshot)
{
    std::atomic_store (&data, std::make_shared<const SequenceBankData> (snapshot));
}

void SequenceBank::setRecording (bool enabled, int slot, int step) noexcept
{
    const auto selectedSlot = (uint32_t) juce::jlimit (0, SequenceBankData::kNumSlots - 1, slot);
    const auto selectedStep = (uint32_t) juce::jlimit (0, Sequence::kMaxSteps - 1, step);
    recordLocation.store ((selectedSlot << 8) | selectedStep);
    recording.store (enabled);
}

bool SequenceBank::popRecordedNote (RecordedNote& note) noexcept
{
    const auto read = recordRead.load (std::memory_order_relaxed);
    if (read == recordWrite.load (std::memory_order_acquire))
        return false;
    note = recordQueue[(size_t) read];
    recordRead.store ((read + 1) % kRecordQueueSize, std::memory_order_release);
    return true;
}

void SequenceBank::recordMidiNote (int midiNote) noexcept
{
    if (! recording.load())
        return;
    const auto write = recordWrite.load (std::memory_order_relaxed);
    const auto next = (write + 1) % kRecordQueueSize;
    if (next == recordRead.load (std::memory_order_acquire))
    {
        droppedRecordCount.fetch_add (1);
        return;
    }
    const auto location = recordLocation.load();
    recordQueue[(size_t) write] = { (int) ((location >> 8) & 0xff), (int) (location & 0xff),
                                    juce::jlimit (-48, 48, midiNote - 60) };
    recordWrite.store (next, std::memory_order_release);
}

juce::ValueTree SequenceBank::toValueTree (int layer) const
{
    juce::MemoryBlock payload (kPayloadSize);
    auto* bytes = static_cast<uint8_t*> (payload.getData());
    const auto current = snapshot();
    for (int slot = 0; slot < SequenceBankData::kNumSlots; ++slot)
    {
        const auto& sequence = current->slots[(size_t) slot];
        bytes[(size_t) slot] = sequence.length;
        for (int step = 0; step < Sequence::kMaxSteps; ++step)
        {
            const auto& value = sequence.steps[(size_t) step];
            const auto offset = stepOffset (slot, step);
            for (int note = 0; note < SequenceStep::kMaxNotes; ++note)
                bytes[offset + (size_t) note] = (uint8_t) value.notes[(size_t) note];
            bytes[offset + SequenceStep::kMaxNotes] = value.noteCount;
            bytes[offset + SequenceStep::kMaxNotes + 1] = (uint8_t) ((value.slide ? 1 : 0)
                | (value.accent ? 2 : 0) | (value.rest ? 4 : 0));
            bytes[offset + SequenceStep::kMaxNotes + 2] = value.gatePercent;
        }
    }

    juce::ValueTree state ("SEQUENCER");
    state.setProperty ("schemaVersion", 1, nullptr);
    state.setProperty ("layer", layer, nullptr);
    state.setProperty ("data", juce::var (payload), nullptr);
    return state;
}

bool SequenceBank::restoreFromValueTree (const juce::ValueTree& state, int expectedLayer, juce::String* error)
{
    const auto encoded = state.getProperty ("data");
    const auto* payload = encoded.getBinaryData();
    if (! state.hasType ("SEQUENCER") || (int) state.getProperty ("schemaVersion", 0) != 1
        || (int) state.getProperty ("layer", -1) != expectedLayer
        || payload == nullptr || payload->getSize() != kPayloadSize)
    {
        if (error) *error = "Sequence bank has an unsupported schema, layer, or payload size.";
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*> (payload->getData());
    auto staged = std::make_shared<SequenceBankData>();
    for (int slot = 0; slot < SequenceBankData::kNumSlots; ++slot)
    {
        auto& sequence = staged->slots[(size_t) slot];
        sequence.length = bytes[(size_t) slot];
        if (sequence.length < 1 || sequence.length > Sequence::kMaxSteps)
        {
            if (error) *error = "Sequence slot contains an invalid sequence length.";
            return false;
        }
        for (int step = 0; step < Sequence::kMaxSteps; ++step)
        {
            auto& value = sequence.steps[(size_t) step];
            const auto offset = stepOffset (slot, step);
            for (int note = 0; note < SequenceStep::kMaxNotes; ++note)
                value.notes[(size_t) note] = (int8_t) bytes[offset + (size_t) note];
            value.noteCount = bytes[offset + SequenceStep::kMaxNotes];
            const auto flags = bytes[offset + SequenceStep::kMaxNotes + 1];
            if ((flags & ~7u) != 0)
            {
                if (error) *error = "Sequence step contains unknown flags.";
                return false;
            }
            value.slide = (flags & 1u) != 0;
            value.accent = (flags & 2u) != 0;
            value.rest = (flags & 4u) != 0;
            value.gatePercent = bytes[offset + SequenceStep::kMaxNotes + 2];
            if (! validStep (value))
            {
                if (error) *error = "Sequence step contains an invalid note, gate, or note count.";
                return false;
            }
        }
    }

    std::atomic_store (&data, std::shared_ptr<const SequenceBankData> (std::move (staged)));
    return true;
}
} // namespace nsynth
