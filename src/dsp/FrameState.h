#pragma once

#include <cstdint>
#include <type_traits>

#include "dsp/DspConfig.h"

namespace hyperlight {

// What the capture layer currently believes about its permission to listen.
//
// Probing is a real state and not an error: the TCC prompt is asynchronous, so
// for the first seconds of a first launch we genuinely do not know, and
// treating that as denial produces a false accusation every single time.
enum class PermissionState : std::int32_t {
    Unknown = 0,
    Probing,
    Granted,
    LikelyDenied,
    Unsupported,
};

// One frame of analysed audio, handed from the DSP thread to the renderer.
//
// Trivially copyable on purpose: it is published through a seqlock, which
// memcpys it wholesale. No pointers, no containers, no virtuals.
struct FrameState
{
    std::uint32_t seq = 0;            // monotonic; the presenter skips repeats
    std::int32_t bandCount = dsp::kBandCount;
    float bars[dsp::kMaxBandCount] = {};   // 0..1, post-ballistics
    float peaks[dsp::kMaxBandCount] = {};  // 0..1, peak-hold dots
    float presence = 0.0f;            // 0..1 global alpha
    float rmsDb = -120.0f;
    float agcDb = 0.0f;
    std::uint32_t formatEpoch = 0;
    std::int32_t permission = static_cast<std::int32_t>(PermissionState::Unknown);
};

static_assert(std::is_trivially_copyable_v<FrameState>,
              "FrameState travels through a seqlock and is memcpy'd wholesale");
static_assert(sizeof(FrameState) < 512);

} // namespace hyperlight
