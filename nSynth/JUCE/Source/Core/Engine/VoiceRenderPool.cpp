#include "VoiceRenderPool.h"

namespace nsynth
{
class VoiceRenderPool::Worker final : public juce::Thread
{
public:
    Worker (VoiceRenderPool& pool, int lane)
        : juce::Thread ("nSynth voice renderer " + juce::String (lane)), owner (pool), laneIndex (lane)
    {
    }

    void dispatch (void* taskContext, RenderFunction taskFunction) noexcept
    {
        context = taskContext;
        function = taskFunction;
        indexedMode = false;
        wake.signal();
    }

    void dispatchIndexed() noexcept
    {
        indexedMode = true;
        wake.signal();
    }

    void stop()
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (-1);
    }

private:
    void run() override
    {
        for (;;)
        {
            wake.wait();
            if (threadShouldExit())
                return;

            if (indexedMode)
            {
                const int count = owner.totalItemCount;
                void* ctx = owner.activeContext;
                auto fn = owner.activeIndexedFunction;
                for (;;)
                {
                    const int item = owner.nextItemIndex.fetch_add (1, std::memory_order_relaxed);
                    if (item >= count)
                        break;
                    fn (ctx, item, laneIndex);
                }
            }
            else if (function != nullptr)
            {
                function (context, laneIndex);
            }

            if (owner.remainingWorkers.fetch_sub (1, std::memory_order_acq_rel) == 1)
                owner.completed.signal();
        }
    }

    VoiceRenderPool& owner;
    const int laneIndex;
    juce::WaitableEvent wake;
    void* context { nullptr };
    RenderFunction function { nullptr };
    bool indexedMode { false };
};

VoiceRenderPool::VoiceRenderPool() = default;

VoiceRenderPool::~VoiceRenderPool()
{
    stopWorkers();
}

void VoiceRenderPool::prepare (int requestedWorkers)
{
    stopWorkers();

    const int cpuCount = juce::SystemStats::getNumCpus();
    const int workerCount = requestedWorkers < 0 ? cpuCount - 1 : requestedWorkers;
    const int boundedCount = juce::jlimit (0, 19, workerCount);

    workers.reserve ((size_t) boundedCount);
    for (int i = 0; i < boundedCount; ++i)
    {
        auto worker = std::make_unique<Worker> (*this, i + 1);
        if (! worker->startThread (juce::Thread::Priority::highest))
            break;
        workers.push_back (std::move (worker));
    }
}

void VoiceRenderPool::render (int workerCount, void* context, RenderFunction function) noexcept
{
    jassert (workerCount >= 0 && workerCount <= getNumWorkers());
    if (workerCount <= 0)
    {
        function (context, 0);
        return;
    }

    remainingWorkers.store (workerCount, std::memory_order_release);
    for (int i = 0; i < workerCount; ++i)
        workers[(size_t) i]->dispatch (context, function);

    function (context, 0);
    while (remainingWorkers.load (std::memory_order_acquire) != 0)
        completed.wait();
}

void VoiceRenderPool::renderItems (int itemCount, int workerCount, void* context, IndexedFunction function) noexcept
{
    if (itemCount <= 0)
        return;

    const int activeWorkers = juce::jlimit (0, getNumWorkers(), juce::jmin (workerCount, itemCount - 1));
    if (activeWorkers <= 0)
    {
        for (int i = 0; i < itemCount; ++i)
            function (context, i, 0);
        return;
    }

    activeContext = context;
    activeIndexedFunction = function;
    totalItemCount = itemCount;
    nextItemIndex.store (0, std::memory_order_relaxed);
    remainingWorkers.store (activeWorkers, std::memory_order_release);

    for (int i = 0; i < activeWorkers; ++i)
        workers[(size_t) i]->dispatchIndexed();

    for (;;)
    {
        const int item = nextItemIndex.fetch_add (1, std::memory_order_relaxed);
        if (item >= itemCount)
            break;
        function (context, item, 0);
    }

    while (remainingWorkers.load (std::memory_order_acquire) != 0)
        completed.wait();
}

void VoiceRenderPool::stopWorkers()
{
    for (auto& worker : workers)
        worker->stop();
    workers.clear();
}
} // namespace nsynth

