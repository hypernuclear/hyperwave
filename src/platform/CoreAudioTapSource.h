#pragma once

#include <atomic>
#include <memory>

#include <QString>
#include <QTimer>

#include "audio/AudioSource.h"
#include "platform/AudioLevelMeter.h"

// Deliberately free of any Objective-C or CoreAudio type. The whole point of
// this header is that App and AudioController can include it without dragging
// AppKit into a translation unit that has no business seeing it.
namespace hyperwave {

class CoreAudioTapSource : public AudioSource
{
    Q_OBJECT

public:
    explicit CoreAudioTapSource(QObject *parent = nullptr);
    ~CoreAudioTapSource() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }

    double sampleRate() const override { return m_sampleRate; }
    std::uint32_t formatEpoch() const override
    {
        return m_formatEpoch.load(std::memory_order_acquire);
    }

    PermissionState permissionState() const override { return m_permission; }
    void setPermissionState(PermissionState state);

    MonoRing *ring() override { return m_ring.get(); }

    std::uint64_t callbackCount() const override
    {
        return m_callbackCount.load(std::memory_order_relaxed);
    }
    std::uint64_t overrunCount() const override
    {
        return m_overruns.load(std::memory_order_relaxed);
    }
    QString statusDetail() const override { return m_statusDetail; }
    AudioProcessList audioProcesses() const override;
    AudioDeviceList audioDevices() const override;
    bool setSourceDeviceUid(const QString &uid) override;
    QString sourceDeviceUid() const override { return m_sourceDeviceUid; }

    // True when the aggregate had to include the default output device, which
    // makes us an output client of the user's speakers. Surfaced because it
    // changes the risk profile enough to be worth seeing in the HUD.
    bool isOutputClient() const { return m_isOutputClient; }

    // --- called from the realtime IO thread, and from nowhere else ---
    // Public only because a C IOProc trampoline has to reach it.
    int renderCallback(const void *inputBufferList, void *outputBufferList) noexcept;

private Q_SLOTS:
    void rebuildAfterHardwareChange();

private:
    friend struct CoreAudioImpl;

    bool createTap();
    bool buildAggregate(bool includeOutputDevice);
    bool startIo();
    void stopIo();
    void destroyAggregate();
    void destroyTap();
    void installListeners();
    void removeListeners();
    void sweepOrphanedTaps();
    void refreshOutputDevice();
    void refreshMeterProcesses();
    bool restartCapture();
    void noteHardwareChange();

    std::unique_ptr<MonoRing> m_ring;   // declared first: destroyed last

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    QTimer m_rebuildDebounce;
    // Metering is separate from capture: one extra aggregate holding one tap
    // per candidate process, so "which apps are audible" is measured rather
    // than inferred from a property that cannot answer it.
    mutable AudioLevelMeter m_meter;
    QTimer m_meterRefresh;    // debounce after a process-list change
    QTimer m_meterFallback;   // slow backstop in case a notification is missed

    std::atomic<std::uint64_t> m_callbackCount{0};
    std::atomic<std::uint64_t> m_overruns{0};
    std::atomic<std::uint32_t> m_formatEpoch{0};
    std::atomic<bool> m_shuttingDown{false};

    double m_sampleRate = 0.0;
    bool m_running = false;
    bool m_isOutputClient = false;
    PermissionState m_permission = PermissionState::Unknown;
    QString m_sourceDeviceUid;  // empty means the whole system mix
    QString m_sourceNote;       // why the requested source was not honoured
    QString m_statusDetail;
};

} // namespace hyperwave
