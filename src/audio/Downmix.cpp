#include "audio/Downmix.h"

#include <cmath>

namespace hyperwave {

void downmixInterleaved(const float *src, float *dst, std::uint32_t frames,
                        std::uint32_t channels) noexcept
{
    if (!src || !dst || channels == 0)
        return;
    if (channels == 1) {
        for (std::uint32_t i = 0; i < frames; ++i)
            dst[i] = src[i];
        return;
    }
    const float inv = 1.0f / static_cast<float>(channels);
    for (std::uint32_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (std::uint32_t c = 0; c < channels; ++c)
            sum += src[i * channels + c];
        dst[i] = sum * inv;
    }
}

void downmixPlanar(const float *const *channelData, float *dst, std::uint32_t frames,
                   std::uint32_t channels) noexcept
{
    if (!channelData || !dst || channels == 0)
        return;
    for (std::uint32_t i = 0; i < frames; ++i)
        dst[i] = 0.0f;
    for (std::uint32_t c = 0; c < channels; ++c) {
        const float *src = channelData[c];
        if (!src)
            continue;
        for (std::uint32_t i = 0; i < frames; ++i)
            dst[i] += src[i];
    }
    const float inv = 1.0f / static_cast<float>(channels);
    for (std::uint32_t i = 0; i < frames; ++i)
        dst[i] *= inv;
}

void sanitize(float *data, std::uint32_t frames) noexcept
{
    if (!data)
        return;
    for (std::uint32_t i = 0; i < frames; ++i) {
        if (!std::isfinite(data[i]))
            data[i] = 0.0f;
    }
}

float rmsDb(const float *data, std::uint32_t frames) noexcept
{
    if (!data || frames == 0)
        return -120.0f;
    double sum = 0.0;
    for (std::uint32_t i = 0; i < frames; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    const double mean = sum / static_cast<double>(frames);
    if (mean <= 1e-12)
        return -120.0f;
    return static_cast<float>(10.0 * std::log10(mean));
}

bool isExactlyZero(const float *data, std::uint32_t frames) noexcept
{
    if (!data)
        return true;
    for (std::uint32_t i = 0; i < frames; ++i) {
        if (data[i] != 0.0f)
            return false;
    }
    return true;
}

} // namespace hyperwave
