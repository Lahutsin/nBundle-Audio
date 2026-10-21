#include "TestHelpers.h"
#include "Core/Engine/SequenceBank.h"

using namespace nsynth;

class SeqTests : public juce::UnitTest
{
public:
    SeqTests() : juce::UnitTest ("Sequencer", "nSynth") {}

    void runTest() override
    {
        beginTest ("16 slots contain 64 editable steps with chord and articulation data");
        {
            SequenceBank bank;
            SequenceStep step;
            step.notes = { -12, 0, 7, 0, 0, 0, 0, 0 };
            step.noteCount = 3;
            step.slide = true;
            step.accent = true;
            step.rest = false;
            step.gatePercent = 72;
            expect (bank.setLength (15, 64));
            expect (bank.setStep (15, 63, step));

            const auto snapshot = bank.snapshot();
            expectEquals ((int) snapshot->slots.size(), 16);
            expectEquals ((int) snapshot->slots[15].length, 64);
            expect (snapshot->slots[15].steps[63] == step);
            expect (snapshot->slots[0].steps[0].rest);
        }

        beginTest ("sequence bank survives XML round trip without losing flags or chord notes");
        {
            SequenceBank source, restored;
            SequenceStep step;
            step.notes = { -24, -12, 0, 12, 24, 0, 0, 0 };
            step.noteCount = 5;
            step.slide = true;
            step.rest = false;
            step.gatePercent = 43;
            expect (source.setLength (4, 48));
            expect (source.setStep (4, 31, step));
            auto xml = source.toValueTree (1).createXml();
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                juce::String error;
                expect (restored.restoreFromValueTree (juce::ValueTree::fromXml (*xml), 1, &error), error);
                expect (*restored.snapshot() == *source.snapshot());
            }
        }

        beginTest ("invalid slot, step, gate, note range, layer and serialized payload are rejected");
        {
            SequenceBank bank;
            expect (! bank.setLength (16, 8));
            expect (! bank.setLength (0, 65));
            expect (! bank.setStep (0, 64, SequenceStep {}));

            SequenceStep invalid;
            invalid.rest = false;
            invalid.noteCount = 1;
            invalid.notes[0] = 49;
            expect (! bank.setStep (0, 0, invalid));
            invalid.notes[0] = 0;
            invalid.gatePercent = 0;
            expect (! bank.setStep (0, 0, invalid));

            auto malformed = bank.toValueTree (0);
            malformed.setProperty ("layer", 1, nullptr);
            juce::String error;
            expect (! bank.restoreFromValueTree (malformed, 0, &error));
            expect (error.isNotEmpty());
            malformed = bank.toValueTree (0);
            malformed.setProperty ("data", juce::var (juce::MemoryBlock (3)), nullptr);
            expect (! bank.restoreFromValueTree (malformed, 0, &error));
            expect (error.isNotEmpty());
        }

        beginTest ("clearSlot resets all 64 steps and restores the default length");
        {
            SequenceBank bank;
            SequenceStep step;
            step.notes[0] = 5;
            step.noteCount = 1;
            step.rest = false;
            expect (bank.setLength (2, 64));
            expect (bank.setStep (2, 10, step));
            expect (bank.clearSlot (2));
            expectEquals ((int) bank.snapshot()->slots[2].length, 16);
            expect (bank.snapshot()->slots[2].steps[10].rest);
        }

        beginTest ("MIDI recording queue is lock-free, captures slot/step/interval and reports overflow");
        {
            SequenceBank bank;
            bank.setRecording (true, 5, 12);
            bank.recordMidiNote (67);
            SequenceBank::RecordedNote note;
            expect (bank.popRecordedNote (note));
            expectEquals (note.slot, 5);
            expectEquals (note.step, 12);
            expectEquals (note.interval, 7);
            expect (! bank.popRecordedNote (note));

            for (int i = 0; i < SequenceBank::kRecordQueueSize + 10; ++i)
                bank.recordMidiNote (60);
            expectGreaterThan (bank.takeDroppedRecordCount(), 0);
            bank.setRecording (false, 0, 0);
            const int dropped = bank.takeDroppedRecordCount();
            expectEquals (dropped, 0);
        }
    }
};

static SeqTests seqTests;
