#pragma once

#include "dsp/DspConfig.h"

namespace hyperlight {

// The part of the chain that turns correct numbers into a display that feels
// musical: attack/release smoothing, falling peak dots, slow automatic gain,
// and the global presence fade.
//
// Deliberately free of any dependency beyond DspConfig so it can be stepped
// deterministically in a unit test, one frame at a time, with no audio.
class Ballistics
{
public:
    void reset();

    // One display frame.
    //
    // `levelDb` holds bandCount post-tilt band levels; `frameRmsDb` is the RMS
    // of the analysis window. Fills `barsOut` and `peaksOut` with 0..1 values
    // and updates presence. The AGC gain computed here is applied on the NEXT
    // frame, which is why agcDb() is a getter rather than an output.
    void process(const float *levelDb, int bandCount, float frameRmsDb,
                 float *barsOut, float *peaksOut);

    float agcDb() const { return m_agcDb; }
    float presence() const { return m_presence; }

private:
    float m_bar[dsp::kMaxBandCount] = {};
    float m_peak[dsp::kMaxBandCount] = {};
    float m_peakVel[dsp::kMaxBandCount] = {};
    int m_peakHold[dsp::kMaxBandCount] = {};

    float m_agcDb = 0.0f;
    float m_presence = 0.0f;
    int m_silentFrames = 0;
};

} // namespace hyperlight
