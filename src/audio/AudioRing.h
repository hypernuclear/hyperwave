#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hyperlight {

// Lock-free single-producer / single-consumer float ring.
//
// Producer is the CoreAudio HAL IO thread, which is a realtime thread: it may
// not allocate, may not take a lock, and may not block. Consumer is the DSP
// thread. Free-running 64-bit cursors indexed by mask, so there is no
// full-versus-empty ambiguity and no modulo in the hot path.
template <std::size_t Capacity>
class AudioRing
{
    static_assert(Capacity && !(Capacity & (Capacity - 1)), "Capacity must be a power of two");
    static constexpr std::uint64_t kMask = Capacity - 1;

public:
    // --- producer side (realtime thread) ---
    //
    // All-or-nothing: a partial write would split one callback's audio across
    // two analysis frames, which is worse than dropping it. Returns false when
    // full so the caller can count the overrun.
    bool write(const float *src, std::uint32_t n, std::uint32_t epoch) noexcept
    {
        const std::uint64_t w = m_write.load(std::memory_order_relaxed);
        const std::uint64_t r = m_read.load(std::memory_order_acquire);
        if (Capacity - (w - r) < n)
            return false;

        const std::size_t i = static_cast<std::size_t>(w & kMask);
        const std::size_t first = std::min<std::size_t>(n, Capacity - i);
        std::memcpy(m_data + i, src, first * sizeof(float));
        if (first < n)
            std::memcpy(m_data, src + first, (n - first) * sizeof(float));

        m_epoch.store(epoch, std::memory_order_relaxed);
        // Release: everything memcpy'd above happens-before the consumer's
        // acquire load of this cursor. Relaxing it works fine on x86 and fails
        // intermittently on arm64, which is the worst possible kind of bug.
        m_write.store(w + n, std::memory_order_release);
        return true;
    }

    // --- consumer side (DSP thread) ---
    std::uint64_t available() const noexcept
    {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed);
    }

    std::uint32_t read(float *dst, std::uint32_t n) noexcept
    {
        const std::uint64_t r = m_read.load(std::memory_order_relaxed);
        const std::uint64_t w = m_write.load(std::memory_order_acquire);
        const auto got = static_cast<std::uint32_t>(std::min<std::uint64_t>(n, w - r));

        const std::size_t i = static_cast<std::size_t>(r & kMask);
        const std::size_t first = std::min<std::size_t>(got, Capacity - i);
        std::memcpy(dst, m_data + i, first * sizeof(float));
        if (first < got)
            std::memcpy(dst + first, m_data, (got - first) * sizeof(float));

        m_read.store(r + got, std::memory_order_release);
        return got;
    }

    // Freshness policy. If the consumer has fallen far behind -- a device
    // rebuild, a thermal stall, a long stall in the renderer -- we jump forward
    // rather than grinding through stale audio. A visualiser running 300 ms
    // behind the music is worse than one that dropped 300 ms.
    //
    // Only the consumer mutates m_read, so this stays SPSC-legal.
    void discardAllButNewest(std::uint64_t keep) noexcept
    {
        const std::uint64_t w = m_write.load(std::memory_order_acquire);
        const std::uint64_t r = m_read.load(std::memory_order_relaxed);
        if (w - r > keep)
            m_read.store(w - keep, std::memory_order_release);
    }

    void reset() noexcept
    {
        m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::uint32_t epoch() const noexcept { return m_epoch.load(std::memory_order_relaxed); }
    static constexpr std::size_t capacity() { return Capacity; }

private:
    alignas(64) std::atomic<std::uint64_t> m_write{0};
    alignas(64) std::atomic<std::uint64_t> m_read{0};
    alignas(64) std::atomic<std::uint32_t> m_epoch{0};
    alignas(64) float m_data[Capacity] = {};
};

// 256 KB. About 1.4 s at 48 kHz, 0.7 s at 96 kHz -- roughly eighty consumer
// periods of headroom, enough to survive a device rebuild or a thermally
// throttled machine without dropping a sample. Smaller buys nothing; bigger
// only grows the worst-case staleness we would then discard anyway.
using MonoRing = AudioRing<65536>;

} // namespace hyperlight
