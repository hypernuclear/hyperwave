#include "dsp/SpectrumAnalyzer.h"

#include <algorithm>
#include <cstring>

#include "audio/Downmix.h"

namespace hyperlight {

SpectrumAnalyzer::SpectrumAnalyzer() = default;

void SpectrumAnalyzer::configure(double sampleRate, int bandCount)
{
    if (sampleRate <= 0.0)
        return;

    m_sampleRate = sampleRate;
    m_bandCount = std::clamp(bandCount, 1, dsp::kMaxBandCount);

    const int n = dsp::fftSizeForRate(sampleRate);
    if (!m_fft || m_fft->size() != n) {
        m_fft = std::make_unique<Fft>(n);
        m_window.resize(n);
        m_rolling.assign(static_cast<std::size_t>(n), 0.0f);
        m_windowed.assign(static_cast<std::size_t>(n), 0.0f);
        m_power.assign(static_cast<std::size_t>(m_fft->bins()), 0.0f);
    } else {
        std::fill(m_rolling.begin(), m_rolling.end(), 0.0f);
    }

    m_levelDb.assign(static_cast<std::size_t>(m_bandCount), dsp::kFloorDb);
    m_mapper.rebuild(m_bandCount, n, sampleRate, dsp::kFreqLo, dsp::kFreqHi);

    // The rolling buffer holds samples at the OLD rate, so it has to go with
    // the tables. Skipping this is what makes every AirPods connect flash one
    // garbage frame before it settles.
    m_filled = 0;
    m_exactZeroFrames = 0;
    m_lastRmsDb = -120.0f;
}

void SpectrumAnalyzer::pushSamples(const float *mono, std::uint32_t frames)
{
    if (!m_fft || !mono || frames == 0)
        return;

    const std::size_t n = m_rolling.size();

    if (frames >= n) {
        // More than a full window arrived at once. Only the newest matters.
        std::memcpy(m_rolling.data(), mono + (frames - n), n * sizeof(float));
        m_filled = n;
        return;
    }

    // Shift the window down and append. At 800 samples of hop against a 4096
    // sample window this moves 13 KB per frame, sixty times a second -- well
    // under a millisecond of memcpy per second, which is not worth the
    // complexity of a second circular buffer to avoid.
    const std::size_t keep = n - frames;
    std::memmove(m_rolling.data(), m_rolling.data() + frames, keep * sizeof(float));
    std::memcpy(m_rolling.data() + keep, mono, frames * sizeof(float));
    m_filled = std::min(n, m_filled + frames);
}

bool SpectrumAnalyzer::analyze(FrameState &state)
{
    if (!m_fft || m_filled < m_rolling.size() || !m_mapper.isValid())
        return false;

    const auto n = static_cast<std::uint32_t>(m_rolling.size());

    m_lastRmsDb = rmsDb(m_rolling.data(), n);
    if (isExactlyZero(m_rolling.data(), n))
        ++m_exactZeroFrames;
    else
        m_exactZeroFrames = 0;

    m_window.apply(m_rolling.data(), m_windowed.data());
    m_fft->powerSpectrum(m_windowed.data(), m_window.sum(), m_power.data());
    m_mapper.aggregate(m_power.data(), m_levelDb.data());

    runBallistics(state, m_levelDb.data());
    return true;
}

void SpectrumAnalyzer::analyzeSilent(FrameState &state)
{
    // Used by the idle throttle once we already know the input is silent:
    // everything decays towards the floor, and the FFT is simply not run.
    m_lastRmsDb = -120.0f;
    ++m_exactZeroFrames;

    if (m_levelDb.size() != static_cast<std::size_t>(m_bandCount))
        m_levelDb.assign(static_cast<std::size_t>(m_bandCount), dsp::kFloorDb);
    std::fill(m_levelDb.begin(), m_levelDb.end(), dsp::kFloorDb);

    runBallistics(state, m_levelDb.data());
}

void SpectrumAnalyzer::runBallistics(FrameState &state, const float *levelDb)
{
    m_ballistics.process(levelDb, m_bandCount, m_lastRmsDb, state.bars, state.peaks);
    state.bandCount = m_bandCount;
    state.presence = m_ballistics.presence();
    state.agcDb = m_ballistics.agcDb();
    state.rmsDb = m_lastRmsDb;
    state.seq = ++m_seq;
}

void SpectrumAnalyzer::reset()
{
    std::fill(m_rolling.begin(), m_rolling.end(), 0.0f);
    m_filled = 0;
    m_exactZeroFrames = 0;
    m_lastRmsDb = -120.0f;
    m_ballistics.reset();
}

} // namespace hyperlight
