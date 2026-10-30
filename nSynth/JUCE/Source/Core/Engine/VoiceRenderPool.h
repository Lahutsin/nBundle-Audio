#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace nsynth
{
// Persistent voice-render workers that help the audio thread within one block.
//
// - Workers run as real-time threads (falling back to the highest normal priority where the OS
//   refuses) and join the host's audio workgroup when it provides one (macOS), so they are
//   scheduled like the audio thread instead of competing with it.
// - All plug-in instances of a process share one worker budget (logical CPUs - 1): a host that
//   already runs instances in parallel is not oversubscribed by every instance adding a full set.
class VoiceRenderPool
{
public:
    using IndexedFunction = void (*) (void*, int, int) noexcept;

    VoiceRenderPool();
    ~VoiceRenderPool();

    VoiceRenderPool (const VoiceRenderPool&) = delete;
    VoiceRenderPool& operator= (const VoiceRenderPool&) = delete;

    // A negative count asks for one worker per logical CPU besides the audio thread; the
    // process-wide budget may grant fewer. blockSize / sampleRate size the real-time budget.
    void prepare (int requestedWorkers = -1, int blockSize = 512, double sampleRate = 48000.0);
    int getNumWorkers() const noexcept { return (int) workers.size(); }

    // Workers join this workgroup before their next job (any thread).
    void setWorkgroup (const juce::AudioWorkgroup& workgroup);

    // Distributes itemCount independent work items (0..itemCount-1) dynamically across
    // caller (lane 0) and up to workerCount persistent workers using atomic work-stealing.
    void renderItems (int itemCount, int workerCount, void* context, IndexedFunction function) noexcept;

private:
    class Worker;
    void stopWorkers();

    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<int> remainingWorkers { 0 };
    std::atomic<int> nextItemIndex { 0 };
    int totalItemCount { 0 };
    void* activeContext { nullptr };
    IndexedFunction activeIndexedFunction { nullptr };
    juce::WaitableEvent completed;

    juce::SpinLock workgroupLock;
    juce::AudioWorkgroup workgroup;
    std::atomic<uint32_t> workgroupGeneration { 0 };

    static std::atomic<int> processWorkers;
};
} // namespace nsynth
