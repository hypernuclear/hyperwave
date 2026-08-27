#include "app/DspWorker.h"

#include <algorithm>

#include "audio/Downmix.h"

namespace hyperwave {

namespace {

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
    if (!m_ring)
        return;

    // A format change means the rolling buffer holds samples at the old rate
    // and the band tables are built for the wrong bin spacing. Reconfiguring
    // throws both away; skipping this is what makes every AirPods connect flash
    // one garbage frame before it settles.
    if (m_epoch != m_appliedEpoch && m_sampleRate > 0.0) {
        m_analyzer.configure(m_sampleRate, dsp::kBandCount);
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
    m_ring->discardAllButNewest(window * 2);

    const auto available = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(m_ring->available(), window * 2));

    if (available > 0) {
        if (m_scratch.size() < available)
            m_scratch.resize(available);
        const std::uint32_t got = m_ring->read(m_scratch.data(), available);
        m_analyzer.pushSamples(m_scratch.data(), got);
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
