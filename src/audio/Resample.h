#pragma once

#include <cmath>
#include <cstdint>

namespace hyperwave {

// Streaming linear-interpolation resampler, mono.
//
// For the microphone, which has its own device and its own rate. The analysis
// runs at the output's rate and the input is summed into it sample for sample,
// so a mic at 96k mixed into a 48k analysis landed an octave low -- and a
// MacBook Pro's own mic runs at 96k. Linear is plenty: the result is reduced
// to a few dozen band levels, not listened to.
//
// Callable from a realtime thread: no allocation, state carried across calls,
// so a stream processed in blocks of any size comes out the same.
class LinearResampler
{
public:
    // Input samples consumed per output sample: inRate / outRate. A step of 1
    // is a straight copy.
    void setStep(double step) noexcept { m_step = step > 0.0 ? step : 1.0; }
    double step() const noexcept { return m_step; }

    void reset() noexcept
    {
        m_pos = 0.0;
        m_last = 0.0f;
    }

    // Writes at most `capacity` samples to dst and returns how many. Input
    // beyond what the capacity covers is dropped, not deferred: a visualiser
    // wants current audio, not a queue.
    std::uint32_t process(const float *src, std::uint32_t n, float *dst,
                          std::uint32_t capacity) noexcept
    {
        if (n == 0)
            return 0;
        std::uint32_t out = 0;
        // m_pos indexes the block, with -1 meaning the previous block's last
        // sample, so interpolation runs straight across block boundaries.
        const double end = static_cast<double>(n) - 1.0;
        while (m_pos < end && out < capacity) {
            const double fl = std::floor(m_pos);
            const auto i = static_cast<std::int64_t>(fl);
            const float frac = static_cast<float>(m_pos - fl);
            const float a = i < 0 ? m_last : src[i];
            const float b = src[i + 1];
            dst[out++] = a + (b - a) * frac;
            m_pos += m_step;
        }
        m_pos -= static_cast<double>(n);
        // A capacity cut leaves m_pos further back than one block; skip ahead
        // to the new block rather than interpolating from audio already gone.
        if (m_pos < -1.0)
            m_pos = -1.0;
        m_last = src[n - 1];
        return out;
    }

private:
    double m_step = 1.0;
    double m_pos = 0.0;
    float m_last = 0.0f;
};

} // namespace hyperwave
