#pragma once

#include <memory>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "audio/AudioSource.h"
#include "dsp/FrameState.h"

namespace hyperwave {

// Owns the capture source and decides what we believe about permission.
//
// Lives entirely on the main thread, which is where every CoreAudio control
// call belongs -- the HAL property listeners are registered on the main queue
// precisely so that all of this can be single-threaded.
class AudioController : public QObject
{
    Q_OBJECT

public:
    explicit AudioController(QObject *parent = nullptr);
    ~AudioController() override;

    bool start();
    void stop();

    AudioSource *source() { return m_source.get(); }
    PermissionState permission() const { return m_permission; }

    // Called by the app when the DSP reports how long the input has been
    // bitwise silent. One half of the two-signal test.
    void noteSilenceRun(int frames);

    // Sends the user to System Settings and arms a re-probe for when they come
    // back. A grant made over there produces no notification on our side.
    void requestPermission();

    // Escalation ladder for recovering a newly granted permission. Whether a
    // grant reaches an existing tap, needs the tap recreated, or only takes
    // effect after a relaunch is not documented anywhere, so we try each in
    // turn rather than guessing.
    void retryAfterGrant();
    bool needsRestartToRecover() const { return m_recoveryStage >= 2; }

Q_SIGNALS:
    void permissionChanged(int state);
    void formatChanged(double sampleRate, unsigned epoch);

private Q_SLOTS:
    void evaluatePermission();

private:
    void setPermission(PermissionState state);

    std::unique_ptr<AudioSource> m_source;
    QTimer m_probeTimer;
    QElapsedTimer m_sinceStart;

    PermissionState m_permission = PermissionState::Unknown;
    int m_silenceRun = 0;
    int m_recoveryStage = 0;
    bool m_everHeardAudio = false;
};

} // namespace hyperwave
