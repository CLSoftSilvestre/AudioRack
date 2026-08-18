#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace audiorack
{

/** Single-producer / single-consumer lock-free ring buffer.

    Classic Lamport queue with acquire/release pairing (see e.g. Herlihy &
    Shavit, "The Art of Multiprocessor Programming", §3.6): the producer only
    writes `head`, the consumer only writes `tail`. A slot is published by the
    release-store of `head` and observed by the acquire-load in pop(), which
    orders the payload copy correctly without any locks or CAS loops.

    Deliberately JUCE-free so it can be unit-tested (including under TSan)
    without pulling in the framework.

    Capacity must be a power of two; usable capacity is Capacity - 1.
    T must be trivially copyable — commands carry raw pointers, never owners
    with destructors.
*/
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert (Capacity >= 2, "Capacity must be at least 2");
    static_assert (std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    /// Producer thread only. Returns false if the queue is full.
    bool push (const T& value) noexcept
    {
        const auto h    = head.load (std::memory_order_relaxed);
        const auto next = (h + 1) & kMask;

        if (next == tail.load (std::memory_order_acquire))
            return false;

        buffer[h] = value;
        head.store (next, std::memory_order_release);
        return true;
    }

    /// Consumer thread only. Returns false if the queue is empty.
    bool pop (T& out) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);

        if (t == head.load (std::memory_order_acquire))
            return false;

        out = buffer[t];
        tail.store ((t + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// Approximate — exact only when called from the consumer thread.
    bool empty() const noexcept
    {
        return head.load (std::memory_order_acquire) == tail.load (std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer {};

    // Padded to separate cache lines so producer and consumer don't false-share.
    alignas (64) std::atomic<std::size_t> head { 0 };
    alignas (64) std::atomic<std::size_t> tail { 0 };
};

} // namespace audiorack
