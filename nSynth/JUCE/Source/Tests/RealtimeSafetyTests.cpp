// Real-time safety and the preset / MIDI behaviour that depends on it: no heap traffic on the
// audio thread, shared data freed off the audio thread, atomic preset loads, per-layer wave banks,
// undo, MPE pitch, voice reuse and MIDI learn.
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include <cstdlib>
#include <new>

namespace
{
// Counts heap allocations and frees made by a thread while it is inside an AllocationGuard.
thread_local int allocationGuardDepth = 0;
std::atomic<int> guardedHeapOperations { 0 };

struct AllocationGuard
{
    AllocationGuard() { guardedHeapOperations = 0; ++allocationGuardDepth; }
    ~AllocationGuard() { --allocationGuardDepth; }
    int count() const { return guardedHeapOperations.load(); }
};

void* allocate (std::size_t size)
{
    if (allocationGuardDepth > 0)
        ++guardedHeapOperations;
    if (auto* p = std::malloc (size > 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void release (void* p) noexcept
{
    if (p != nullptr && allocationGuardDepth > 0)
        ++guardedHeapOperations;
    std::free (p);
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }

using namespace nsynth;
using namespace nsynth::test;

class RealtimeSafetyTests : public juce::UnitTest
{
public:
    RealtimeSafetyTests() : juce::UnitTest ("RealtimeSafety", "nSynth") {}

    struct Rig
    {
        explicit Rig (int blockSize = 256) : block (blockSize), buffer (2, blockSize)
        {
            processor.setRateAndBufferSizeDetails (48000.0, block);
            processor.prepareToPlay (48000.0, block);
        }

        void set (const juce::String& id, float plain) { setParam (processor.getState(), id, plain); }
        void setLayer (Layer l, const char* suffix, float plain) { set (layerParamId (l, suffix), plain); }

        // One block; returns the peak of the left channel.
        float process (const juce::MidiBuffer& midi = {})
        {
            processor.getEngine().process (buffer, nullptr, nullptr, midi, 120.0);
            float peakValue = 0.0f;
            for (int i = 0; i < block; ++i)
                peakValue = juce::jmax (peakValue, std::abs (buffer.getSample (0, i)));
            return peakValue;
        }

        std::vector<float> render (int samples, const std::vector<std::pair<int, juce::MidiMessage>>& events = {})
        {
            std::vector<float> left;
            for (int position = 0; position < samples; position += block)
            {
                juce::MidiBuffer midi;
                for (const auto& [at, message] : events)
                    if (at >= position && at < position + block)
                        midi.addEvent (message, at - position);
                process (midi);
                for (int i = 0; i < block; ++i)
                    left.push_back (buffer.getSample (0, i));
            }
            return left;
        }

        NSynthAudioProcessor processor;
        int block;
        juce::AudioBuffer<float> buffer;
    };

    static juce::MidiMessage cc (int channel, int number, int value) { return juce::MidiMessage::controllerEvent (channel, number, value); }
    static juce::MidiMessage on (int note, int channel = 1) { return juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100); }
    static juce::MidiMessage off (int note, int channel = 1) { return juce::MidiMessage::noteOff (channel, note); }

    static std::shared_ptr<WaveBank> customBank (int slot)
    {
        auto bank = std::make_shared<WaveBank> (*WaveBank::factory());
        std::vector<float> cycle (MipWavetable::kTableSize);
        for (int i = 0; i < MipWavetable::kTableSize; ++i)
            cycle[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * 5.0 * i / MipWavetable::kTableSize);
        bank->setAltCycle (slot, cycle.data());
        return bank;
    }

    void runTest() override
    {
        beginTest ("the audio thread neither allocates nor frees while playing, editing and swapping banks");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::unison, 1.0f);
            rig.setLayer (Layer::upper, pid::reverbOn, 1.0f);
            rig.setLayer (Layer::upper, pid::delayOn, 1.0f);
            auto& engine = rig.processor.getEngine();
            engine.modMatrix (Layer::upper).addRoute (ModSource::lfo1, findLayerSpecIndex (pid::lpf), 0.3f);
            engine.modMatrix (Layer::upper).addRoute (ModSource::macro1, findLayerSpecIndex (pid::reverbMix), 0.4f);
            rig.process();   // first block: lazily initialised statics

            juce::MidiBuffer chord, release, controllers, empty;
            for (int note : { 48, 55, 60, 64, 67 })
                chord.addEvent (on (note), 3);
            chord.addEvent (cc (1, 64, 127), 10);
            for (int note : { 48, 55, 60, 64, 67 })
                release.addEvent (off (note), 5);
            release.addEvent (cc (1, 64, 0), 100);
            release.addEvent (on (60), 120);
            for (int i = 0; i < 64; ++i)
            {
                controllers.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + i * 60), i * 4);
                controllers.addEvent (cc (1, 1, i), i * 4 + 1);
                controllers.addEvent (cc (1, 74, 127 - i), i * 4 + 2);
            }

            int heapOperations = 0;
            for (int round = 0; round < 4; ++round)
            {
                // Editor-side changes between blocks: new matrix snapshots and a new wave bank.
                engine.modMatrix (Layer::upper).addRoute (ModSource::env2, findLayerSpecIndex (pid::res), 0.1f * (float) (round + 1));
                engine.setWaveBank (Layer::upper, round % 2 == 0 ? WaveBankPtr (customBank (round)) : WaveBank::factory());

                const AllocationGuard guard;
                for (auto* midi : { &chord, &empty, &controllers, &release, &empty })
                    rig.process (*midi);
                heapOperations += guard.count();
            }
            expectEquals (heapOperations, 0);
        }

        beginTest ("replaced snapshots and wave banks are freed off the audio thread, once it moved on");
        {
            Rig rig;
            auto& engine = rig.processor.getEngine();
            std::weak_ptr<const WaveBank> first;
            {
                auto bank = customBank (2);
                first = bank;
                engine.setWaveBank (Layer::upper, bank);
            }
            rig.process();                                  // the audio thread uses the custom bank
            engine.setWaveBank (Layer::upper, WaveBank::factory());
            engine.collectGarbage();
            expect (! first.expired(), "still in use by the audio thread until its next block");
            rig.process();                                  // moves on to the factory bank
            expect (! first.expired(), "the audio thread never drops the last reference");
            engine.collectGarbage();
            expect (first.expired(), "freed by the editing thread");
        }

        beginTest ("each layer has its own wave bank: a patch only changes the layer it is loaded into");
        {
            Rig rig;
            auto& processor = rig.processor;
            auto& engine = processor.getEngine();
            engine.setWaveBank (Layer::upper, customBank (4));
            const auto upperHash = engine.getWaveBank (Layer::upper)->getContentHash();
            const nSynth::PresetEntry* factoryPatch = nullptr;
            for (const auto& entry : nSynth::PresetLibrary::getFactoryEntries())
                if (entry.kind == nSynth::PresetKind::patch)
                {
                    factoryPatch = &entry;
                    break;
                }
            expect (factoryPatch != nullptr);
            juce::String error;
            expect (processor.loadPatch (Layer::lower, *factoryPatch, &error), error);
            expect (engine.getWaveBank (Layer::upper)->getContentHash() == upperHash);
            expect (engine.getWaveBank (Layer::lower)->hasFactoryContent());

            // A saved patch carries its waves into the other layer; a factory patch resets them.
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("nsynth_layer_waves", ".nsynthpatch");
            expect (processor.savePatch (Layer::upper, file, &error), error);
            expect (processor.loadPatch (Layer::lower, file, &error), error);
            expect (engine.getWaveBank (Layer::lower)->getContentHash() == upperHash);
            expect (processor.loadPatch (Layer::upper, *factoryPatch, &error), error);
            expect (engine.getWaveBank (Layer::upper)->hasFactoryContent());
            expect (engine.getWaveBank (Layer::lower)->getContentHash() == upperHash);

            // Performances store both banks.
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            Rig other;
            other.processor.setStateInformation (state.getData(), (int) state.getSize());
            expect (other.processor.getEngine().getWaveBank (Layer::upper)->hasFactoryContent());
            expect (other.processor.getEngine().getWaveBank (Layer::lower)->getContentHash() == upperHash);
            file.deleteFile();
        }

        beginTest ("a preset load suspends the layer: fade-out, silence, then a clean restart");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            juce::MidiBuffer note;
            note.addEvent (on (69), 0);
            rig.process (note);
            for (int i = 0; i < 8; ++i)
                rig.process();
            expect (rig.process() > 0.05f);

            auto& engine = rig.processor.getEngine();
            engine.beginStateChange (SynthEngine::upperScope);
            // A parameter change while suspended is not heard: the layer keeps its last values.
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::saw));
            const float fading = rig.process();
            expect (fading > 0.0f, "the first block fades out instead of cutting");
            rig.process();
            expectEquals (rig.process(), 0.0f, "silent after 10 ms");
            expect (engine.getActiveVoices (Layer::upper) > 0, "voices are kept (not rendered) while suspended");
            engine.endStateChange (SynthEngine::upperScope);
            rig.process();
            expectEquals (engine.getActiveVoices (Layer::upper), 0, "the old patch's voices are dropped");
        }

        beginTest ("loading a patch into one layer leaves the other layer playing (DUAL)");
        {
            Rig rig;
            rig.set (perfParamId (pid::kbdMode), (float) toInt (KeyboardMode::dual));
            rig.process();
            juce::MidiBuffer note;
            note.addEvent (on (60), 0);
            rig.process (note);
            expect (rig.processor.getEngine().getActiveVoices (Layer::upper) > 0);
            expect (rig.processor.getEngine().getActiveVoices (Layer::lower) > 0);
            rig.processor.initLayer (Layer::upper);
            rig.process();
            expectEquals (rig.processor.getEngine().getActiveVoices (Layer::upper), 0);
            expect (rig.processor.getEngine().getActiveVoices (Layer::lower) > 0);
        }

        beginTest ("a performance with a non-finite parameter value is rejected and leaves the sound untouched");
        {
            Rig rig;
            const auto id = layerParamId (Layer::upper, pid::lpf);
            rig.set (id, 1234.0f);
            juce::MemoryBlock state;
            rig.processor.getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") == id)
                    child->setAttribute ("value", "nan");
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("nsynth_nan", ".nsynthperf");
            expect (xml->writeTo (file));
            rig.set (id, 2000.0f);
            juce::String error;
            expect (! rig.processor.loadPerformance (file, &error));
            expect (error.isNotEmpty());
            expectWithinAbsoluteError (rig.processor.getState().getParameter (id)->convertFrom0to1 (
                                           rig.processor.getState().getParameter (id)->getValue()), 2000.0f, 1.0f);
            file.deleteFile();
        }

        beginTest ("undo / redo step over settled edits of parameters and modulation");
        {
            Rig rig;
            auto& processor = rig.processor;
            auto* cutoff = processor.getState().getParameter (layerParamId (Layer::upper, pid::lpf));
            const float original = cutoff->getValue();
            expect (! processor.canUndo());

            cutoff->setValueNotifyingHost (0.25f);
            processor.captureUndoPoint();   // still moving
            cutoff->setValueNotifyingHost (0.2f);
            processor.captureUndoPoint();
            processor.captureUndoPoint();   // settled: one step
            processor.getEngine().modMatrix (Layer::upper).addRoute (ModSource::env1, findLayerSpecIndex (pid::res), 0.5f);
            processor.captureUndoPoint();
            processor.captureUndoPoint();
            expect (processor.canUndo());

            expect (processor.undo());
            expect (! processor.getEngine().modMatrix (Layer::upper).hasSource (ModSource::env1));
            expectWithinAbsoluteError (cutoff->getValue(), 0.2f, 1.0e-6f);
            expect (processor.undo());
            expectWithinAbsoluteError (cutoff->getValue(), original, 1.0e-6f);
            expect (! processor.undo());
            expect (processor.redo());
            expectWithinAbsoluteError (cutoff->getValue(), 0.2f, 1.0e-6f);
            expect (processor.canRedo());
        }

        beginTest ("MPE: the zone message sets a 48 semitone member bend range, independent of BEND DDS");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            rig.setLayer (Layer::upper, pid::bendDds, 12.0f);
            // Lower zone with 15 members; member channel 2 bends by +1/4 = +12 semitones.
            const auto out = rig.render (24064, { { 0, cc (1, 101, 0) }, { 0, cc (1, 100, 6) }, { 0, cc (1, 6, 15) },
                                                  { 1, juce::MidiMessage::pitchWheel (2, 8192 + 2048) }, { 2, on (69, 2) } });
            expectWithinAbsoluteError (zeroCrossingFrequency (out, 48000.0, 4800), 880.0, 1.5);
        }

        beginTest ("repeating a note reuses the unit that is still releasing it");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::env2Release, 3.0f);
            rig.render (2048, { { 0, on (60) }, { 512, off (60) }, { 1024, on (60) } });
            expectEquals (rig.processor.getEngine().getActiveVoices (Layer::upper), 1);
            rig.render (512, { { 0, on (64) } });
            expectEquals (rig.processor.getEngine().getActiveVoices (Layer::upper), 2);
        }

        beginTest ("MIDI learn ignores the sustain pedal and channel mode messages");
        {
            Rig rig;
            auto& engine = rig.processor.getEngine();
            const auto id = layerParamId (Layer::upper, pid::res);
            expect (engine.requestMidiLearn (id));
            rig.render (256, { { 0, cc (1, 64, 127) }, { 1, cc (1, 64, 0) }, { 2, cc (1, 123, 0) } });
            expectEquals (engine.getMidiLearnCC (id), -1);
            rig.render (256, { { 0, cc (1, 20, 90) } });
            expectEquals (engine.getMidiLearnCC (id), 20);
        }

        beginTest ("factory performances are host programs; program 0 keeps the current sound");
        {
            Rig rig;
            auto& processor = rig.processor;
            expect (processor.getNumPrograms() > 1);
            processor.setCurrentProgram (0);
            expectEquals (processor.getCurrentPresetName(), juce::String ("Init"));
            processor.setCurrentProgram (2);
            expectEquals (processor.getCurrentProgram(), 2);
            expectEquals (processor.getCurrentPresetName(), processor.getProgramName (2));
        }

        beginTest ("a layer without voices sleeps once its tails died away and wakes on the next note");
        {
            Rig rig;
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::reverbOn, 1.0f);
            auto& upper = rig.processor.getEngine().layer (Layer::upper);
            rig.render (4800, { { 0, on (60) }, { 2400, off (60) } });
            expect (! upper.isSilenced(), "the reverb tail is still sounding");
            for (int i = 0; i < 2000 && ! upper.isSilenced(); ++i)
                rig.process();
            expect (upper.isSilenced(), "asleep after the tail");
            expect (rig.processor.getEngine().layer (Layer::lower).isSilenced(), "the unused layer in SINGLE mode sleeps");

            juce::MidiBuffer note;
            note.addEvent (on (69), 100);   // mid-block
            const float level = rig.process (note);
            expect (! upper.isSilenced());
            expect (level > 0.0f, "the note sounds in the block it starts in");
        }

        beginTest ("MASTER VOLUME changes ramp across the block instead of stepping");
        {
            Rig rig (512);
            rig.setLayer (Layer::upper, pid::binaural, 0.0f);
            rig.setLayer (Layer::upper, pid::dds1Wave, (float) toInt (Dds1Wave::sine));
            juce::MidiBuffer note;
            note.addEvent (on (69), 0);
            rig.process (note);
            for (int i = 0; i < 6; ++i)
                rig.process();
            const float before = rig.process();
            rig.set (perfParamId (pid::masterVolume), -60.0f);
            rig.process();
            float head = 0.0f;
            for (int i = 0; i < 64; ++i)
                head = juce::jmax (head, std::abs (rig.buffer.getSample (0, i)));
            expect (head > 0.5f * before, "the first samples still play near the old level");
            expect (rig.process() < 0.01f * before);
        }
    }
};

static RealtimeSafetyTests realtimeSafetyTests;
