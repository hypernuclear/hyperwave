#pragma once

#include <atomic>
#include <vector>

#include <QObject>
#include <QTimer>

#include "audio/AudioRing.h"
#include "dsp/FrameState.h"
#include "dsp/SeqLock.h"
#include "dsp/SpectrumAnalyzer.h"

namespace hyperwave {

// Owns the analysis loop and lives on its own thread.
//
// Not on the main thread, and the reason is specific rather than reflexive:
// NSMenu runs a modal run loop while the user has the status item's menu open,
// which starves ordinary main-thread work. A main-thread FFT would freeze the
// visualiser at exactly the moment somebody clicked it to look at it. A
// dedicated thread also decouples the analysis rate from the display rate, so a
// 120 Hz ProMotion panel does not double the DSP cost for nothing.
class DspWorker : public QObject
{
    Q_OBJECT

public:
    explicit DspWorker(QObject *parent = nullptr);

    // The renderer reads through this. Safe to call from any thread, including
    // before the worker has started.
    const SeqLock<FrameState> *frames() const { return &m_frames; }

    // Set once, before the thread starts.
    void setRing(MonoRing *ring) { m_ring = ring; }

    // Summed into the analysis when present. Its own ring because it has its
    // own clock: two producers on one single-producer ring is not a ring.
    void setInputRing(MonoRing *ring) { m_inputRing = ring; }
    // Drive the analysis from a synthetic signal instead of the capture ring.
    //
    // Synthesised into the *input* rather than faked at the output, so the FFT,
    // the band mapping and the ballistics all run exactly as they do on real
    // audio. A fake that skipped them would be a different code path, and the
    // one thing a renderer preview must not do is show you something the real
    // pipeline would never produce.
    void setFakeAudio(bool fake) { m_fakeAudio = fake; }

public Q_SLOTS:
    void start();
    void stop();

    // Both arrive as queued connections from the main thread.
    void setFormat(double sampleRate, unsigned epoch);
    void setPermissionState(int state);

Q_SIGNALS:
    // Silence/audio edges, so the main thread can pause and resume the display
    // link. Emitted on transitions only, never per frame.
    void audioBecameActive();
    void audioBecameSilent();

    // How many consecutive frames have been bitwise silent. The permission
    // state machine on the main thread weighs this against the process probe.
    void silenceRunChanged(int frames);

private Q_SLOTS:
    void tick();

private:
    void applyIdleThrottle(bool silent);

    MonoRing *m_ring = nullptr;
    MonoRing *m_inputRing = nullptr;
    std::vector<float> m_inputScratch;
    SpectrumAnalyzer m_analyzer;
    SeqLock<FrameState> m_frames;
    // Created in start(), which runs on this object's own thread. A QTimer
    // constructed alongside the worker would belong to whichever thread built
    // the worker, and Qt refuses to start a timer from a foreign thread --
    // silently leaving the analysis loop dead while the ring overflows behind
    // it. Parenting it to `this` at construction is not an alternative: the
    // parent would then try to delete a non-heap member.
    QTimer *m_timer = nullptr;

    std::vector<float> m_scratch;
    bool m_fakeAudio = false;
    double m_fakePhase[6] = {};
    double m_fakeSeconds = 0.0;
    std::uint32_t m_fakeNoise = 22222u;

    double m_sampleRate = 0.0;
    std::uint32_t m_epoch = 0;
    std::uint32_t m_appliedEpoch = 0;
    std::atomic<int> m_pendingPermission{static_cast<int>(PermissionState::Unknown)};

    bool m_active = false;      // last edge we reported
    int m_silentFrames = 0;
    bool m_throttled = false;
};

} // namespace hyperwave
