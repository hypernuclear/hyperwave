#pragma once

#include <cstdint>

namespace hyperwave {

// Interleaved N-channel float -> mono, averaged.
//
// Lives in shared code rather than inside the CoreAudio callback so it can be
// unit-tested and so the Windows backend gets identical behaviour. Callable
// from a realtime thread: no allocation, no branching on anything but counts.
void downmixInterleaved(const float *src, float *dst, std::uint32_t frames,
                        std::uint32_t channels) noexcept;

// Planar (one buffer per channel) float -> mono, averaged. `channelData` holds
// `channels` pointers, each with `frames` samples. Null channel pointers are
// treated as silence rather than skipped, so a partially populated buffer list
// does not quietly change the mix gain.
void downmixPlanar(const float *const *channelData, float *dst, std::uint32_t frames,
                   std::uint32_t channels) noexcept;

// Replaces any non-finite sample with zero. One NaN reaching the FFT poisons
// every band permanently -- the ballistics have no path back from NaN.
void sanitize(float *data, std::uint32_t frames) noexcept;

// Root mean square of a mono block, in dBFS. Returns -120 for silence rather
// than -inf so it can be compared and smoothed without special cases.
float rmsDb(const float *data, std::uint32_t frames) noexcept;

// True only if every sample is bitwise zero. Deliberately exact rather than
// thresholded: digital silence really is 0.0f, and "the tap is producing
// literally nothing" is a different fact from "the music is quiet". The
// permission probe depends on being able to tell those two apart.
bool isExactlyZero(const float *data, std::uint32_t frames) noexcept;

} // namespace hyperwave
