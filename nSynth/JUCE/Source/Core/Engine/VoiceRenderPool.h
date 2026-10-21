#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace nsynth
{
class VoiceRenderPool
{
public:
    using RenderFunction = void (*) (void*, int) noexcept;
    using IndexedFunction = void (*) (void*, int, int) noexcept;

    VoiceRenderPool();
    ~VoiceRenderPool();

    VoiceRenderPool (const VoiceRenderPool&) = delete;
    VoiceRenderPool& operator= (const VoiceRenderPool&) = delete;

    // A negative count selects one worker per logical CPU besides the audio thread.
    void prepare (int requestedWorkers = -1);
    int getNumWorkers() const noexcept { return (int) workers.size(); }

    // Runs lane 0 on the caller and lanes 1..workerCount on persistent workers.
    void render (int workerCount, void* context, RenderFunction function) noexcept;

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
};
} // namespace nsynth

