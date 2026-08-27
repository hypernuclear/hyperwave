#pragma once

#include <cmath>

// Every tuning constant in the analysis chain lives here and nowhere else.
//
// The two you will actually want to move are kFloorDb and kCeilDb. They decide
// where "quiet" and "loud" sit on the strip, and they are pure taste; expect to
// nudge them by a few dB after listening to a handful of real tracks. Keeping
// them here means that is a one-line change rather than a hunt.
namespace hyperwave::dsp {

inline constexpr int kBandCount = 28;
inline constexpr int kMaxBandCount = 32;   // sizes the fixed arrays in FrameState

inline constexpr float kFreqLo = 40.0f;
inline constexpr float kFreqHi = 16000.0f;

// Bins outside this range never reach the band mapper. The low cut matters:
// some codecs emit real DC, and without it band 0 sits pegged at full height
// forever. The high cut is mostly codec artefacts and dither.
inline constexpr float kAnalysisLoHz = 20.0f;
inline constexpr float kAnalysisHiHz = 18000.0f;

inline constexpr float kFrameHz = 60.0f;
inline constexpr float kFrameSec = 1.0f / kFrameHz;
inline constexpr float kFrameMs = 1000.0f / kFrameHz;   // 16.667

// Analysis window length, as a duration. The FFT size is the power of two
// nearest this at the current sample rate, clamped to [1024, 8192].
//
// 85 ms buys 11.7 Hz bins at 48 kHz, which is what keeps the bass bars moving
// independently instead of as one blob. It costs time resolution, but on a
// 2pt-wide bar the eye reads punch from the 25 ms attack ballistic, not from
// the analysis window. If kicks ever feel mushy, halving this is the first
// experiment -- and it is deliberately a single number so that it can be.
inline constexpr float kWindowSec = 0.085f;
inline constexpr int kFftSizeMin = 1024;
inline constexpr int kFftSizeMax = 8192;

// Aesthetic tilt, applied on top of the (already flat for pink noise) band
// sums. Real music rolls off faster than pink up top and runs hotter than pink
// down low, so a gentle treble lift plus a small bass shelf tracks what people
// expect to see better than a flat response does.
inline constexpr float kTiltDbPerOctave = 1.5f;
inline constexpr float kBassShelfDb = -3.0f;
inline constexpr float kBassShelfHz = 80.0f;
inline constexpr float kBassShelfEndHz = 160.0f;

// Tuned by watching real audio rather than by derivation. At -72 the display
// span was 57 dB, and since broadband music spreads roughly 20 dB across the
// bands, every bar sat in a narrow ribbon around two thirds height -- correct,
// and boring. A 45 dB span maps that same 20 dB spread across nearly half the
// strip, which is what makes it dance.
inline constexpr float kFloorDb = -60.0f;
inline constexpr float kCeilDb = -15.0f;

// Spatial smoothing kernel across neighbouring bands. One pass only: at 28
// bars, unsmoothed neighbours look noisy and digital, and two passes turn the
// whole thing to mush.
inline constexpr float kSmoothSide = 0.15f;
inline constexpr float kSmoothCentre = 0.70f;

// Bar ballistics. Exponential, coefficient = exp(-frame / tau).
inline constexpr float kAttackMs = 25.0f;
inline constexpr float kReleaseMs = 180.0f;

// Peak-hold dots. The fall is accelerating rather than linear -- a dot that
// descends at constant speed reads mechanical, one that accelerates reads like
// it is falling. Two extra floats per band for a large part of the character.
inline constexpr float kPeakHoldMs = 500.0f;
inline constexpr float kPeakGravity = 1.6f;   // units per second squared

// Slow automatic gain, so a quiet track still moves.
//
// Strength is deliberately below 1: at full correction every track looks
// identical, which throws away the one thing a visualiser is for. At 0.75 a
// quiet track reaches roughly three quarters the height of a loud one.
inline constexpr float kAgcAttackMs = 300.0f;    // gain drops fast when it gets loud
inline constexpr float kAgcReleaseMs = 4000.0f;  // and rises slowly when it gets quiet
inline constexpr float kAgcTargetU = 0.90f;
inline constexpr float kAgcMinDb = -6.0f;
inline constexpr float kAgcMaxDb = 24.0f;
inline constexpr float kAgcStrength = 0.75f;
inline constexpr float kAgcFreezeRmsDb = -68.0f; // below this we chase the noise floor

// Presence: the global fade that takes the strip down when nothing is playing.
// It fades, never vanishes -- a menu bar item that disappears reads as a crash.
inline constexpr float kSilenceRmsDb = -68.0f;
inline constexpr float kSilenceHoldMs = 400.0f;
inline constexpr float kPresenceFadeOutMs = 600.0f;
inline constexpr float kPresenceFadeInMs = 120.0f;
inline constexpr float kIdlePresence = 0.35f;    // resting opacity, not zero

inline float coeff(float tauMs)
{
    return std::exp(-kFrameMs / tauMs);
}

// The power of two nearest kWindowSec at this rate, clamped.
inline int fftSizeForRate(double sampleRate)
{
    int n = 1;
    const int want = static_cast<int>(kWindowSec * sampleRate);
    while (n < want && n < kFftSizeMax)
        n <<= 1;
    if (n < kFftSizeMin)
        n = kFftSizeMin;
    if (n > kFftSizeMax)
        n = kFftSizeMax;
    return n;
}

} // namespace hyperwave::dsp
