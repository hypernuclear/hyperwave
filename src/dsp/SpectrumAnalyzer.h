#pragma once

#include <memory>
#include <vector>

#include "dsp/Ballistics.h"
#include "dsp/BandMapper.h"
#include "dsp/Fft.h"
#include "dsp/FrameState.h"
#include "dsp/Window.h"

namespace hyperlight {

// Owns the whole analysis chain for one audio stream and produces one
// FrameState per display frame.
//
// Deliberately knows nothing about threads, Qt or platforms -- DspWorker owns
// the timer and the thread affinity, this owns the maths. That split is what
// lets the tests drive it a frame at a time from synthetic audio.
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();

    // Rebuilt on every format epoch. Choosing the FFT size from the sample rate
    // keeps the analysis window a constant ~85 ms rather than a constant number
    // of samples, so the display behaves identically at 44.1 and at 96 kHz.
    void configure(double sampleRate, int bandCount);

    // `mono` holds `frames` samples, newest last. They are appended to the
    // rolling analysis buffer; the FFT runs over the newest fftSize() of them.
    void pushSamples(const float *mono, std::uint32_t frames);

    // Runs one analysis frame over whatever has accumulated and folds the
    // result into `state`. Returns false when there is not yet a full window.
    bool analyze(FrameState &state);

    // Cheap path for the idle throttle: updates presence and the bar decay
    // without paying for an FFT. Used once we know the input is silent.
    void analyzeSilent(FrameState &state);

    int fftSize() const { return m_fft ? m_fft->size() : 0; }
    double sampleRate() const { return m_sampleRate; }
    int bandCount() const { return m_bandCount; }
    float lastRmsDb() const { return m_lastRmsDb; }

    // How many consecutive analysis frames have been bitwise silent. The
    // permission probe reads this: a tap we are not allowed to hear delivers
    // exact zeros, and so does a machine playing nothing, which is exactly why
    // this alone is never enough to conclude denial.
    int exactZeroFrames() const { return m_exactZeroFrames; }

    void reset();

private:
    void runBallistics(FrameState &state, const float *levelDb);

    std::unique_ptr<Fft> m_fft;
    HannWindow m_window;
    BandMapper m_mapper;
    Ballistics m_ballistics;

    double m_sampleRate = 0.0;
    int m_bandCount = dsp::kBandCount;

    std::vector<float> m_rolling;   // fftSize samples, oldest first
    std::size_t m_filled = 0;
    std::vector<float> m_windowed;
    std::vector<float> m_power;
    std::vector<float> m_levelDb;

    float m_lastRmsDb = -120.0f;
    int m_exactZeroFrames = 0;
    std::uint32_t m_seq = 0;
};

} // namespace hyperlight
