#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <memory>

namespace nsynth
{
struct SequenceStep
{
    static constexpr int kMaxNotes = 8;
    std::array<int8_t, kMaxNotes> notes {};
    uint8_t noteCount { 0 };
    bool slide { false }, accent { false }, rest { true };
    uint8_t gatePercent { 90 };

    bool operator== (const SequenceStep& other) const noexcept;
};

struct Sequence
{
    static constexpr int kMaxSteps = 64;
    std::array<SequenceStep, kMaxSteps> steps {};
    uint8_t length { 16 };

    bool operator== (const Sequence& other) const noexcept;
};

struct SequenceBankData
{
    static constexpr int kNumSlots = 16;
    std::array<Sequence, kNumSlots> slots {};

    bool operator== (const SequenceBankData& other) const noexcept;
};

class SequenceBank
{
public:
    struct RecordedNote { int slot { 0 }, step { 0 }, interval { 0 }; };
    static constexpr int kRecordQueueSize = 128;

    SequenceBank();

    std::shared_ptr<const SequenceBankData> snapshot() const noexcept { return std::atomic_load (&data); }
    bool setStep (int slot, int step, const SequenceStep& value);
    bool setLength (int slot, int length);
    bool clearSlot (int slot);
    void clearAll();
    void restoreSnapshot (const SequenceBankData& snapshot);
    void setRecording (bool enabled, int slot, int step) noexcept;
    bool isRecording() const noexcept { return recording.load(); }
    bool popRecordedNote (RecordedNote& note) noexcept;
    int takeDroppedRecordCount() noexcept { return droppedRecordCount.exchange (0); }
    void recordMidiNote (int midiNote) noexcept;

    juce::ValueTree toValueTree (int layer) const;
    bool restoreFromValueTree (const juce::ValueTree& state, int expectedLayer, juce::String* error = nullptr);

private:
    std::shared_ptr<const SequenceBankData> data;
    std::array<RecordedNote, kRecordQueueSize> recordQueue {};
    std::atomic<uint32_t> recordWrite { 0 }, recordRead { 0 };
    std::atomic<uint32_t> recordLocation { 0 };
    std::atomic<int> droppedRecordCount { 0 };
    std::atomic<bool> recording { false };
};
} // namespace nsynth
