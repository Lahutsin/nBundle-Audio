#include "VoiceRenderPool.h"

namespace nsynth
{
std::atomic<int> VoiceRenderPool::processWorkers { 0 };

class VoiceRenderPool::Worker final : public juce::Thread
{
public:
    Worker (VoiceRenderPool& pool, int lane)
        : juce::Thread ("nSynth voice renderer " + juce::String (lane)), owner (pool), laneIndex (lane)
    {
    }

    void dispatchIndexed() noexcept
    {
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
            {
                token = {};   // a workgroup is left on the thread that joined it
                return;
            }

            joinCurrentWorkgroup();
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

            if (owner.remainingWorkers.fetch_sub (1, std::memory_order_acq_rel) == 1)
                owner.completed.signal();
        }
    }

    void joinCurrentWorkgroup()
    {
        const auto generation = owner.workgroupGeneration.load (std::memory_order_acquire);
        if (generation == joinedGeneration)
            return;
        joinedGeneration = generation;
        token = {};
        juce::AudioWorkgroup group;
        {
            const juce::SpinLock::ScopedLockType lock (owner.workgroupLock);
            group = owner.workgroup;
        }
        if (group)
            group.join (token);
    }

    VoiceRenderPool& owner;
    const int laneIndex;
    juce::WaitableEvent wake;
    juce::WorkgroupToken token;
    uint32_t joinedGeneration { 0 };
};

VoiceRenderPool::VoiceRenderPool() = default;

VoiceRenderPool::~VoiceRenderPool()
{
    stopWorkers();
}

void VoiceRenderPool::prepare (int requestedWorkers, int blockSize, double sampleRate)
{
    stopWorkers();

    const int budget = juce::jmax (0, juce::SystemStats::getNumCpus() - 1);
    const int wanted = juce::jlimit (0, 19, requestedWorkers < 0 ? budget : requestedWorkers);
    // Explicit requests (tests, tools) are honoured; automatic ones share the process budget.
    int granted = wanted;
    if (requestedWorkers < 0)
    {
        int used = processWorkers.load();
        do
            granted = juce::jmin (wanted, juce::jmax (0, budget - used));
        while (! processWorkers.compare_exchange_weak (used, used + granted));
    }
    else
    {
        processWorkers.fetch_add (granted);
    }

    const auto options = juce::Thread::RealtimeOptions{}
                             .withPriority (8)
                             .withApproximateAudioProcessingTime (juce::jmax (16, blockSize), juce::jmax (8000.0, sampleRate));
    workers.reserve ((size_t) granted);
    for (int i = 0; i < granted; ++i)
    {
        auto worker = std::make_unique<Worker> (*this, i + 1);
        if (! worker->startRealtimeThread (options) && ! worker->startThread (juce::Thread::Priority::highest))
            break;
        workers.push_back (std::move (worker));
    }
    processWorkers.fetch_sub (granted - (int) workers.size());
}

void VoiceRenderPool::setWorkgroup (const juce::AudioWorkgroup& newWorkgroup)
{
    {
        const juce::SpinLock::ScopedLockType lock (workgroupLock);
        workgroup = newWorkgroup;
    }
    workgroupGeneration.fetch_add (1, std::memory_order_acq_rel);
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
    processWorkers.fetch_sub ((int) workers.size());
    workers.clear();
}
} // namespace nsynth
