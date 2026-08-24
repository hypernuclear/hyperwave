#include "app/AudioController.h"

#include <QDebug>

#include "dsp/DspConfig.h"

#ifdef Q_OS_MACOS
#include "platform/AudioCapturePermission.h"
#include "platform/AudioProcessProbe.h"
#endif

namespace hyperlight {

namespace {

// Nothing may be called a denial inside this window. The TCC prompt is
// asynchronous: the tap can start, deliver silence, and only then have the
// dialog appear. Accusing someone of denying a permission they have not yet
// been asked for is the worst thing this state machine could do.
constexpr qint64 kGraceMs = 8000;

// How long the input has to be bitwise silent, while something else on the
// machine is demonstrably playing, before we conclude we are not being let
// through. Three seconds at 60 Hz.
constexpr int kDeniedSilenceFrames = 180;

constexpr int kProbeIntervalMs = 1000;

} // namespace

AudioController::AudioController(QObject *parent)
    : QObject(parent)
    , m_source(AudioSource::create())
{
    m_probeTimer.setInterval(kProbeIntervalMs);
    connect(&m_probeTimer, &QTimer::timeout, this, &AudioController::evaluatePermission);

    connect(m_source.get(), &AudioSource::formatChanged, this, &AudioController::formatChanged);
    connect(m_source.get(), &AudioSource::permissionStateChanged, this, [this](int state) {
        // The source can only ever report a hard failure to create the tap.
        // Everything softer than that is this class's decision to make.
        const auto reported = static_cast<PermissionState>(state);
        if (reported == PermissionState::LikelyDenied || reported == PermissionState::Unsupported)
            setPermission(reported);
    });
}

AudioController::~AudioController()
{
    stop();
}

bool AudioController::start()
{
    m_sinceStart.start();
    m_silenceRun = 0;
    m_everHeardAudio = false;

    const bool ok = m_source->start();
    if (!ok) {
        setPermission(PermissionState::LikelyDenied);
        return false;
    }

    setPermission(PermissionState::Probing);
    m_probeTimer.start();
    return true;
}

void AudioController::stop()
{
    m_probeTimer.stop();
    if (m_source)
        m_source->stop();
}

void AudioController::noteSilenceRun(int frames)
{
    m_silenceRun = frames;
    if (frames == 0)
        m_everHeardAudio = true;
}

void AudioController::evaluatePermission()
{
    // Once we have actually heard something, the question is settled for the
    // session. Pausing Spotify must never walk this back into an accusation.
    if (m_everHeardAudio) {
        setPermission(PermissionState::Granted);
        m_probeTimer.stop();
        return;
    }

    if (m_permission == PermissionState::Unsupported)
        return;

    // And that is as far as it goes. Silence is never treated as denial.
    //
    // The original design cross-checked a run of exact zeros against
    // kAudioProcessPropertyIsRunningOutput, on the theory that "we hear nothing
    // while something else is demonstrably playing" could only mean we were
    // being refused. That theory was wrong about the property: it reports
    // whether a process holds an open output IO, not whether it is making a
    // sound. Measured on an idle machine, a 3D editor and a paused music player
    // both reported yes with the room silent -- so the probe concluded denial
    // on a working, granted, perfectly healthy tap.
    //
    // There is no property that answers "is this process audible", so there is
    // no second signal to be had. The honest position is that a silent tap is
    // indistinguishable from a silent machine, and the only hard evidence of
    // denial is AudioHardwareCreateProcessTap failing outright, which start()
    // already reports. Everything else stays Probing, and the menu offers a way
    // to check the permission at any time rather than waiting to accuse.
}

void AudioController::requestPermission()
{
#ifdef Q_OS_MACOS
    AudioCapturePermission::openSystemSettings();
    AudioCapturePermission::notifyOnNextActivation(
        [](void *context) { static_cast<AudioController *>(context)->retryAfterGrant(); }, this);
#endif
}

void AudioController::retryAfterGrant()
{
    // Escalate rather than jumping straight to the big hammer, but do not hide
    // the big hammer either: a relaunch is the only thing guaranteed to pick up
    // a new grant, and pretending otherwise costs the user more than offering
    // it does.
    switch (m_recoveryStage) {
    case 0:
        qInfo("hyperlight: re-probing after a possible grant (rebuilding the aggregate)");
        m_source->stop();
        m_source->start();
        break;
    case 1:
        qInfo("hyperlight: still silent; recreating the tap");
        m_source->stop();
        m_source->start();
        break;
    default:
        qInfo("hyperlight: still silent after recreating the tap; a restart is needed");
        break;
    }

    ++m_recoveryStage;
    m_sinceStart.start();
    m_silenceRun = 0;
    setPermission(PermissionState::Probing);
    m_probeTimer.start();
}

void AudioController::setPermission(PermissionState state)
{
    if (m_permission == state)
        return;
    m_permission = state;
    Q_EMIT permissionChanged(static_cast<int>(state));
}

} // namespace hyperlight
