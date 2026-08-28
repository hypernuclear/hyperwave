#include "app/DspWorker.h"

#include <cmath>

#include <algorithm>

#include "audio/Downmix.h"

namespace hyperwave {

namespace {

// MSVC does not define M_PI from <cmath> without _USE_MATH_DEFINES, and this
// file is compiled on every platform. Spelled out to match dsp/Fft.cpp and
// dsp/Window.cpp, which already carry their own kPi for the same reason.
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// After this much continuous silence the analysis drops to a slow poll. It
// still has to run to notice audio coming back, but it does not have to pay
// for an FFT to do that.
constexpr int kIdleThrottleAfterFrames = static_cast<int>(5000.0f / dsp::kFrameMs);
constexpr int kActiveIntervalMs = 16;
constexpr int kIdleIntervalMs = 100;

} // namespace

DspWorker::DspWorker(QObject *parent)
    : QObject(parent)
{
}

void DspWorker::start()
{
    // The timer is built here rather than in the constructor because this slot
    // runs on the worker's own thread. Built alongside the worker it would
    // belong to whoever constructed us, Qt would refuse to start it from a
    // foreign thread, and the analysis loop would sit dead while the ring
    // quietly overflowed behind it.
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(kActiveIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &DspWorker::tick);
    }

    if (m_sampleRate > 0.0) {
        m_analyzer.configure(m_sampleRate, dsp::kBandCount);
        m_appliedEpoch = m_epoch;
    }
    m_timer->start();
}

void DspWorker::stop()
{
    if (m_timer)
        m_timer->stop();
}

void DspWorker::setFormat(double sampleRate, unsigned epoch)
{
    m_sampleRate = sampleRate;
    m_epoch = epoch;
}

void DspWorker::setPermissionState(int state)
{
    m_pendingPermission.store(state, std::memory_order_relaxed);
}

void DspWorker::applyIdleThrottle(bool silent)
{
    const bool wantThrottle = silent && m_silentFrames > kIdleThrottleAfterFrames;
    if (wantThrottle == m_throttled)
        return;
    m_throttled = wantThrottle;
    if (m_timer)
        m_timer->setInterval(m_throttled ? kIdleIntervalMs : kActiveIntervalMs);
}

void DspWorker::tick()
{
    if (!m_ring && !m_fakeAudio)
        return;

    // A format change means the rolling buffer holds samples at the old rate
    // and the band tables are built for the wrong bin spacing. Reconfiguring
    // throws both away; skipping this is what makes every AirPods connect flash
    // one garbage frame before it settles.
    if (m_epoch != m_appliedEpoch && m_sampleRate > 0.0) {
        m_analyzer.configure(m_sampleRate, dsp::kBandCount);
        if (m_ring)
            m_ring->reset();
        m_appliedEpoch = m_epoch;
        return;
    }

    if (m_analyzer.fftSize() == 0)
        return;

    // Resynchronise to fresh audio rather than grinding through a backlog. A
    // visualiser running a third of a second behind the music is worse than one
    // that dropped a third of a second.
    const auto window = static_cast<std::uint64_t>(m_analyzer.fftSize());

    // How many samples this tick produced, whatever produced them. The
    // throttled path below re-reads this block to decide whether audio has
    // come back, so both branches have to leave it meaning the same thing.
    std::uint32_t available = 0;

    if (m_fakeAudio) {
        // Six partials an octave apart, each breathing at its own rate, plus a
        // kick twice a second.
        //
        // Spread across the spectrum deliberately: a single swept tone lights
        // one band at a time and says nothing about how a look behaves when the
        // whole width is busy, which is the case that actually matters.
        const auto n = static_cast<std::uint32_t>(window);
        if (m_scratch.size() < n)
            m_scratch.resize(n);
        const double dt = 1.0 / m_sampleRate;
        for (std::uint32_t i = 0; i < n; ++i) {
            const double t = m_fakeSeconds + i * dt;
            const double beat = std::exp(-std::fmod(t, 0.5) * 6.0);
            double sample = 0.0;
            for (int k = 0; k < 6; ++k) {
                const double freq = 80.0 * std::pow(2.0, k);
                const double breathe = 0.5 + 0.5 * std::sin(t * (0.7 + 0.31 * k) + k);
                const double amp = (k == 0 ? 0.35 + 0.5 * beat : 0.28) * breathe;
                m_fakePhase[k] += kTwoPi * freq * dt;
                if (m_fakePhase[k] > kTwoPi)
                    m_fakePhase[k] -= kTwoPi;
                sample += std::sin(m_fakePhase[k]) * amp;
            }
            // Plus broadband noise, which matters more than it sounds like it
            // should. Six pure partials light six bands and leave the rest at
            // the floor, so a look was being judged against a spectrum no music
            // produces -- the lines in particular looked dead because almost
            // nothing crossed the bloom threshold. Music is broadband; the
            // synthetic stand-in has to be too.
            m_fakeNoise = m_fakeNoise * 1103515245u + 12345u;
            const double noise =
                (static_cast<double>((m_fakeNoise >> 16) & 0x7FFF) / 16384.0) - 1.0;
            sample += noise * 0.22 * (0.4 + 0.6 * beat);

            m_scratch[i] = static_cast<float>(sample * 0.22);
        }
        m_fakeSeconds += n * dt;
        m_analyzer.pushSamples(m_scratch.data(), n);
        available = n;
    } else {
        m_ring->discardAllButNewest(window * 2);

        available = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(m_ring->available(), window * 2));

        if (available > 0) {
            if (m_scratch.size() < available)
                m_scratch.resize(available);
            const std::uint32_t got = m_ring->read(m_scratch.data(), available);

            // Sum the input in, sample for sample as far as it goes.
            //
            // Two devices means two clocks, so the counts drift apart; the
            // input is mixed over as much of the block as it has and the rest
            // is left alone. Summing what is there beats stalling for the rest
            // of it, and beats resampling a signal that is going to be reduced
            // to twenty-eight numbers anyway.
            if (m_inputRing) {
                // Throw away the input's backlog first, exactly as the output
                // ring does above.
                //
                // Without this the microphone ran over a second late and stayed
                // there. Two devices on two clocks never deliver in step, so any
                // excess the input produces accumulates; the ring holds 65,536
                // frames, so it filled up and simply stayed full, and every
                // sample mixed in was a second and a bit old. The output path
                // had always discarded its backlog to stay current -- the input
                // path was the one that did not.
                // Keep only what is about to be mixed, which is far less than
                // the output ring keeps.
                //
                // The output holds two windows because the analyser reads a
                // whole FFT frame out of it. The input is summed into a block
                // that has already been read, so anything beyond that block is
                // not early, it is late: at two windows the microphone sat 85 ms
                // behind, which is inaudible on music you are already hearing
                // and very obvious on a room.
                m_inputRing->discardAllButNewest(got);
                const auto want = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(m_inputRing->available(), got));
                if (want > 0) {
                    if (m_inputScratch.size() < want)
                        m_inputScratch.resize(want);
                    const std::uint32_t heard = m_inputRing->read(m_inputScratch.data(), want);
                    for (std::uint32_t i = 0; i < heard; ++i)
                        m_scratch[i] += m_inputScratch[i];
                    sanitize(m_scratch.data(), got);
                }
            }

            m_analyzer.pushSamples(m_scratch.data(), got);
        }
    }

    FrameState state;
    bool produced = false;

    if (m_throttled) {
        // In the throttled state we only need to know whether audio has come
        // back, which the RMS of the newest block answers without an FFT.
        const bool stillSilent = (available == 0)
            || isExactlyZero(m_scratch.data(), available)
            || rmsDb(m_scratch.data(), available) < dsp::kSilenceRmsDb;
        if (stillSilent) {
            m_analyzer.analyzeSilent(state);
            produced = true;
        } else {
            produced = m_analyzer.analyze(state);
        }
    } else {
        produced = m_analyzer.analyze(state);
    }

    if (!produced)
        return;

    state.formatEpoch = m_appliedEpoch;
    state.permission = m_pendingPermission.load(std::memory_order_relaxed);
    m_frames.write(state);

    const bool silent = state.rmsDb < dsp::kSilenceRmsDb;
    m_silentFrames = silent ? m_silentFrames + 1 : 0;
    applyIdleThrottle(silent);

    // Edges only. The main thread uses these to pause and resume the display
    // link, and it does not need sixty notifications a second to do that.
    const bool nowActive = state.presence > 0.0f;
    if (nowActive != m_active) {
        m_active = nowActive;
        if (m_active)
            Q_EMIT audioBecameActive();
        else
            Q_EMIT audioBecameSilent();
    }

    Q_EMIT silenceRunChanged(m_analyzer.exactZeroFrames());
}

} // namespace hyperwave
