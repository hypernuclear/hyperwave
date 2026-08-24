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

    if (m_sinceStart.elapsed() < kGraceMs)
        return;

    if (m_silenceRun < kDeniedSilenceFrames)
        return;

    // Signal A says we are hearing exact zeros. On its own that is worthless --
    // a machine with nothing playing produces exactly the same thing. Signal B
    // is the independent oracle: if some other process is demonstrably
    // outputting audio and we are still hearing nothing, the difference is us.
#ifdef Q_OS_MACOS
    if (!AudioProcessProbe::isAvailable())
        return;
    if (!AudioProcessProbe::isAnyOtherProcessOutputting()) {
        // Nothing is playing. That is the ordinary resting state of a machine
        // and it is not a problem, so say nothing at all.
        return;
    }
#else
    return;
#endif

    if (m_permission != PermissionState::LikelyDenied) {
        qInfo("hyperlight: something is playing but the tap is silent; "
              "treating system audio access as denied");
        setPermission(PermissionState::LikelyDenied);
    }
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
