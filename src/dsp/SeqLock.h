#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace hyperlight {

// Single-writer, multi-reader publication of a small POD.
//
// The writer never blocks and never waits, which is the whole point: the DSP
// thread must be able to publish a frame without caring whether the presenter
// happens to be reading one. Readers detect a torn read by the sequence number
// changing underneath them and simply try again.
//
// Chosen over a triple buffer because the payload is a few cache lines and both
// sides run at 60 Hz, so the collision window is on the order of 100 ns out of
// every 16.7 ms. A retry is close to never taken, and when it is, skipping one
// vsync is invisible. Forty lines beats a hundred for no measurable difference.
template <typename T>
class SeqLock
{
    static_assert(std::is_trivially_copyable_v<T>, "SeqLock payload must be memcpy-able");

public:
    void write(const T &value) noexcept
    {
        const std::uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_relaxed);   // odd: update in flight
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&m_value, &value, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        m_seq.store(s + 2, std::memory_order_relaxed);   // even: stable again
    }

    // False means the writer kept winning the race. The caller should skip the
    // frame rather than retry forever -- there will be another one in 16 ms.
    bool tryRead(T &out, int maxAttempts = 4) const noexcept
    {
        for (int i = 0; i < maxAttempts; ++i) {
            const std::uint32_t before = m_seq.load(std::memory_order_relaxed);
            if (before & 1u)
                continue;   // writer is mid-update, do not even look
            std::atomic_thread_fence(std::memory_order_acquire);
            std::memcpy(&out, &m_value, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (m_seq.load(std::memory_order_relaxed) == before)
                return true;
        }
        return false;
    }

private:
    alignas(64) std::atomic<std::uint32_t> m_seq{0};
    alignas(64) T m_value{};
};

} // namespace hyperlight
