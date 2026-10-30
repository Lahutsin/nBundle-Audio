#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace nsynth
{
// Hands immutable objects from the editing threads to ONE audio thread without locks, allocations
// or deallocations on the audio thread.
//
// - publish() / latest() / collect(): any non-audio thread (serialised by an internal mutex).
// - acquire(): the audio thread only. It returns the newest object and marks it as in use (a hazard
//   pointer); the object stays valid until the audio thread's next acquire().
//
// Replaced objects are kept on a retired list and freed by publish() / collect() once the audio
// thread no longer uses them, so the audio thread never drops the last reference. Other threads
// share ownership through latest(), which keeps an object alive for as long as they hold it.
template <typename T>
class RealtimeHandoff
{
public:
    RealtimeHandoff() = default;
    explicit RealtimeHandoff (std::shared_ptr<const T> initial) { publish (std::move (initial)); }

    RealtimeHandoff (const RealtimeHandoff&) = delete;
    RealtimeHandoff& operator= (const RealtimeHandoff&) = delete;

    void publish (std::shared_ptr<const T> next)
    {
        const std::lock_guard<std::mutex> guard (lock);
        if (current != nullptr)
            retired.push_back (std::move (current));
        current = std::move (next);
        published.store (current.get(), std::memory_order_seq_cst);
        collectLocked();
    }

    std::shared_ptr<const T> latest() const
    {
        const std::lock_guard<std::mutex> guard (lock);
        return current;
    }

    // Frees retired objects the audio thread has moved past. Returns true when some are still pending.
    bool collect()
    {
        const std::lock_guard<std::mutex> guard (lock);
        collectLocked();
        return ! retired.empty();
    }

    // Audio thread: the newest object (null before the first publish()).
    const T* acquire() noexcept
    {
        const T* object = published.load (std::memory_order_seq_cst);
        for (;;)
        {
            inUse.store (object, std::memory_order_seq_cst);
            const T* check = published.load (std::memory_order_seq_cst);
            if (check == object)
                return object;
            object = check;
        }
    }

private:
    void collectLocked()
    {
        const T* protectedObject = inUse.load (std::memory_order_seq_cst);
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (it->get() != protectedObject)
                it = retired.erase (it);
            else
                ++it;
        }
    }

    mutable std::mutex lock;
    std::shared_ptr<const T> current;
    std::vector<std::shared_ptr<const T>> retired;
    std::atomic<const T*> published { nullptr };
    std::atomic<const T*> inUse { nullptr };
};
} // namespace nsynth
