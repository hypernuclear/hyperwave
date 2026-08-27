#include "dsp/Ballistics.h"

#include <algorithm>
#include <cmath>

namespace hyperwave {

void Ballistics::reset()
{
    for (int k = 0; k < dsp::kMaxBandCount; ++k) {
        m_bar[k] = 0.0f;
        m_peak[k] = 0.0f;
        m_peakVel[k] = 0.0f;
        m_peakHold[k] = 0;
    }
    m_agcDb = 0.0f;
    m_presence = 0.0f;
    m_silentFrames = 0;
}

void Ballistics::process(const float *levelDb, int bandCount, float frameRmsDb,
                         float *barsOut, float *peaksOut)
{
    bandCount = std::min(bandCount, dsp::kMaxBandCount);

    const float attack = dsp::coeff(dsp::kAttackMs);
    const float release = dsp::coeff(dsp::kReleaseMs);

    // --- automatic gain ---
    //
    // Tracks the loudest band and nudges it towards kAgcTargetU. Strength is
    // below 1 on purpose: at full correction every track ends up the same
    // height, which throws away the one thing a visualiser is for.
    float peakDb = -160.0f;
    for (int k = 0; k < bandCount; ++k)
        peakDb = std::max(peakDb, levelDb[k]);

    if (frameRmsDb > dsp::kAgcFreezeRmsDb) {
        const float targetDb = dsp::kFloorDb + dsp::kAgcTargetU * (dsp::kCeilDb - dsp::kFloorDb);
        const float wantDb = std::clamp((targetDb - peakDb) * dsp::kAgcStrength,
                                        dsp::kAgcMinDb, dsp::kAgcMaxDb);
        // Gain coming down (the music got loud) is the fast direction; gain
        // going up (it got quiet) is the slow one. The other way round pumps.
        const float a = (wantDb < m_agcDb) ? dsp::coeff(dsp::kAgcAttackMs)
                                           : dsp::coeff(dsp::kAgcReleaseMs);
        m_agcDb = wantDb + (m_agcDb - wantDb) * a;
    }

    // --- per-band smoothing, then the peak dots ---
    const float span = dsp::kCeilDb - dsp::kFloorDb;
    float u[dsp::kMaxBandCount] = {};
    for (int k = 0; k < bandCount; ++k)
        u[k] = std::clamp((levelDb[k] + m_agcDb - dsp::kFloorDb) / span, 0.0f, 1.0f);

    // One pass of spatial smoothing across neighbours. At 28 bars this is the
    // cheapest visual-quality win available; a second pass turns it to mush.
    float s[dsp::kMaxBandCount] = {};
    for (int k = 0; k < bandCount; ++k) {
        const float l = u[std::max(0, k - 1)];
        const float r = u[std::min(bandCount - 1, k + 1)];
        s[k] = dsp::kSmoothSide * l + dsp::kSmoothCentre * u[k] + dsp::kSmoothSide * r;
    }

    const int holdFrames = static_cast<int>(dsp::kPeakHoldMs / dsp::kFrameMs);

    for (int k = 0; k < bandCount; ++k) {
        const float target = s[k];
        const float a = (target > m_bar[k]) ? attack : release;
        m_bar[k] = target + (m_bar[k] - target) * a;

        // Fall first, then check whether the dot has landed. Doing it in this
        // order means "the bar caught up with the dot" and "the dot caught up
        // with the bar" collapse into one case, which is what they physically
        // are -- the dot is resting on the bar either way.
        // The epsilon is not cosmetic. On a steady passage the AGC drifts the
        // bar by a few millionths per frame, which is enough to keep the bar
        // strictly below the dot and quietly drain the hold timer -- so the
        // first real transient, the one moment the hold exists for, would get
        // whatever was left of it. A tolerance three orders of magnitude below
        // one screen pixel makes "resting on the bar" mean what it looks like.
        constexpr float kLandEpsilon = 1e-4f;

        if (m_bar[k] < m_peak[k] - kLandEpsilon) {
            if (m_peakHold[k] > 0) {
                --m_peakHold[k];
            } else {
                // Accelerating rather than linear. A dot descending at constant
                // speed reads mechanical; one that picks up speed reads like it
                // is falling, and that is most of the character of the display.
                m_peakVel[k] += dsp::kPeakGravity * dsp::kFrameSec;
                m_peak[k] -= m_peakVel[k] * dsp::kFrameSec;
            }
        }

        if (m_bar[k] >= m_peak[k] - kLandEpsilon) {
            // Resting on the bar. Rearming the hold here matters more than it
            // looks: without it a long steady passage leaves the timer at zero,
            // and the next drop sends the dot straight down with no hold at all
            // -- which is the one moment the hold exists for. Resetting the
            // velocity matters for the same reason, only worse: left to
            // accumulate it grows without bound and the dot eventually
            // teleports rather than falls.
            m_peak[k] = m_bar[k];
            m_peakVel[k] = 0.0f;
            m_peakHold[k] = holdFrames;
        }

        barsOut[k] = m_bar[k];
        peaksOut[k] = m_peak[k];
    }

    // --- presence ---
    //
    // Three states, not two. Silence inside the hold window HOLDS; it must not
    // fade in and it must not fade out. Folding the hold into the fade-in
    // direction, which is the easy mistake, means a silent launch brightens the
    // strip to full over a tenth of a second before dropping it again -- a
    // visible flash announcing that nothing is playing.
    const bool silent = frameRmsDb < dsp::kSilenceRmsDb;
    m_silentFrames = silent ? m_silentFrames + 1 : 0;

    if (!silent) {
        m_presence = std::min(1.0f, m_presence + dsp::kFrameMs / dsp::kPresenceFadeInMs);
    } else if (static_cast<float>(m_silentFrames) * dsp::kFrameMs > dsp::kSilenceHoldMs) {
        m_presence = std::max(0.0f, m_presence - dsp::kFrameMs / dsp::kPresenceFadeOutMs);
    }
}

} // namespace hyperwave
