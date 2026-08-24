#include "app/App.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QPalette>
#include <QTimer>

#include "app/DebugHud.h"
#include "app/LowPower.h"
#include "render/Palette.h"

namespace hyperlight {

namespace {

constexpr int kCompactBands = 14;
// A couple of settled frames before the clock is allowed to sleep, so a
// momentary gap between tracks does not stop and restart it.
constexpr int kAtRestFramesBeforeSleep = 4;
constexpr qreal kVerifySeconds = 5.0;

} // namespace

App::App(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_settings(this)
    , m_audio(std::make_unique<AudioController>())
{
    m_dspThread.setObjectName(QStringLiteral("hyperlight.dsp"));
}

App::~App()
{
    // Ordering matters and it is the reverse of construction.
    //
    // The display link goes first so nothing tries to render into a buffer
    // that is about to be freed; then the capture stops, which joins the HAL
    // IO thread and guarantees the ring's producer is gone; only then does the
    // DSP thread wind down and take the consumer with it. Tearing the DSP down
    // first leaves a live producer writing into a ring whose reader has been
    // destroyed, which does not crash immediately and so gets found by a user
    // rather than by us.
    if (m_presenter)
        m_presenter->stopClock();
    if (m_audio)
        m_audio->stop();
    if (m_dspThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, &DspWorker::stop, Qt::BlockingQueuedConnection);
        m_dspThread.quit();
        m_dspThread.wait();
    }
    if (m_presenter)
        m_presenter->remove();
}

bool App::start()
{
    m_worker = new DspWorker;
    m_worker->setRing(m_audio->source()->ring());
    m_worker->moveToThread(&m_dspThread);
    connect(&m_dspThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_audio.get(), &AudioController::formatChanged, m_worker, &DspWorker::setFormat);
    connect(m_audio.get(), &AudioController::permissionChanged, this, &App::onPermissionChanged);
    connect(m_worker, &DspWorker::audioBecameActive, this, &App::onAudioBecameActive);
    connect(m_worker, &DspWorker::audioBecameSilent, this, &App::onAudioBecameSilent);
    connect(m_worker, &DspWorker::silenceRunChanged, m_audio.get(),
            &AudioController::noteSilenceRun);

    m_dspThread.start();

    // Start capture before the DSP loop so the first format is already known by
    // the time the worker configures itself.
    m_audio->start();
    QMetaObject::invokeMethod(m_worker, &DspWorker::setFormat, Qt::QueuedConnection,
                              m_audio->source()->sampleRate(),
                              m_audio->source()->formatEpoch());
    QMetaObject::invokeMethod(m_worker, &DspWorker::start, Qt::QueuedConnection);

    if (m_options.verifyAudio) {
        // A one-liner answer to "can it hear the system", rather than a squint
        // at a 24pt strip.
        auto *timer = new QTimer(this);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, &App::runVerifyTick);
        timer->start();
        return true;
    }

    if (!m_options.windowed) {
        m_presenter = BarPresenter::create(this);
        if (!m_presenter->install()) {
            qCritical("hyperlight: could not create a menu bar item");
            return false;
        }
        connect(m_presenter.get(), &BarPresenter::frameDue, this, &App::onFrameDue);
        connect(m_presenter.get(), &BarPresenter::geometryChanged, this,
                &App::onGeometryChanged);
        onGeometryChanged();
        applyFrameRate();
        m_presenter->startClock();
    }

    if (m_options.windowed || m_options.hud) {
        m_hud = std::make_unique<DebugHud>();
        m_hud->setSource(m_audio->source());
        m_hud->show();

        if (m_options.windowed) {
            // No presenter, so nothing is driving the clock. A plain timer is
            // fine here -- this path exists for iterating on the renderer, not
            // for shipping.
            auto *timer = new QTimer(this);
            timer->setInterval(16);
            connect(timer, &QTimer::timeout, this, &App::onFrameDue);
            timer->start();
            rebuildStyle();
            ensureBuffers();
        }
    }

    return true;
}

void App::rebuildStyle()
{
    const bool compact = m_settings.compact()
        || (m_presenter && m_presenter->prefersCompact());

    m_style = RenderStyle{};
    m_style.bandCount = compact ? kCompactBands : dsp::kBandCount;

    // The menu bar's own foreground colour, for the presets that follow the
    // system rather than carrying their own palette.
    const QColor foreground = QGuiApplication::palette().color(QPalette::WindowText);
    Palette::applyTo(m_style, m_settings.gradientId(), foreground);

    m_style.idleOpacity = dsp::kIdlePresence;
}

void App::ensureBuffers()
{
    const QSize wanted = BarRenderer::pixelSize(m_style, m_heightPt, m_dpr);
    for (QImage &buffer : m_buffers) {
        if (buffer.size() != wanted || buffer.format() != QImage::Format_ARGB32_Premultiplied) {
            buffer = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        }
    }
}

void App::onGeometryChanged()
{
    if (!m_presenter)
        return;
    m_heightPt = m_presenter->heightPt();
    m_dpr = m_presenter->devicePixelRatio();
    rebuildStyle();
    m_presenter->setWidthPt(m_style.stripWidthPt());
    ensureBuffers();
}

bool App::renderCurrentFrame(QImage **out)
{
    FrameState state;
    if (!m_worker->frames()->tryRead(state))
        return false;   // the writer kept winning; there will be another in 16 ms

    // On a 120 Hz display the link fires twice per analysis frame. Presenting
    // the same pixels again costs a full recomposite of the menu bar for
    // nothing, so skip it.
    if (state.seq == m_lastPresentedSeq)
        return false;
    m_lastPresentedSeq = state.seq;

    // Once the picture has genuinely stopped moving, stop the clock.
    //
    // This is the single biggest lever on idle cost, and the reason is not our
    // drawing: a running CADisplayLink drags the whole AppKit
    // deferred-display-link machinery through the run loop on every vsync
    // whether or not we do anything with it. Leaving it running while showing a
    // static row of dots measured the same as running at full tilt.
    m_atRestFrames = frameIsAtRest(state) ? m_atRestFrames + 1 : 0;
    if (m_presenter && m_atRestFrames > kAtRestFramesBeforeSleep) {
        m_presenter->stopClock();
        m_atRestFrames = 0;
    }

    QImage &target = m_buffers[m_nextBuffer];
    m_nextBuffer = 1 - m_nextBuffer;
    if (target.isNull())
        return false;

    m_renderer.render(state, m_style, m_heightPt, m_dpr, target);

    if (m_hud) {
        m_hud->setFrame(state);
        m_hud->setPreview(target);
    }

    if (out)
        *out = &target;
    return true;
}

void App::onFrameDue()
{
    // Do not spend a recomposite on a strip nobody can see.
    if (m_presenter && !m_presenter->isVisibleOnScreen())
        return;

    QImage *rendered = nullptr;
    if (!renderCurrentFrame(&rendered) || !rendered)
        return;

    if (m_presenter)
        m_presenter->present(*rendered);

    // Let the ballistics settle before grabbing. A frame taken at startup is
    // all zeros, which makes for a useless reference image.
    if (!m_options.shotPath.isEmpty() && m_lastPresentedSeq > 120) {
        rendered->save(m_options.shotPath);
        qInfo("hyperlight: wrote %s (%dx%d px, %.1f x %.1f pt @ %.0fx)", qPrintable(m_options.shotPath),
              rendered->width(), rendered->height(), m_style.stripWidthPt(), m_heightPt, m_dpr);
        QCoreApplication::quit();
    }
}

void App::onAudioBecameActive()
{
    if (m_presenter && !m_presenter->isClockRunning())
        m_presenter->startClock();
}

void App::onAudioBecameSilent()
{
    // Nothing to do here beyond noting the edge. The clock is stopped by
    // onFrameDue once the picture has actually stopped changing, which is a
    // later moment than this one: presence has only just reached zero and the
    // bars and peak dots are still falling. Killing the link here would freeze
    // them mid-descent.
}

void App::applyFrameRate()
{
    if (!m_presenter)
        return;

    // Sixty is what the visuals were designed at and it is the default. Thirty
    // is the lever for battery: measured on macOS 26 the strip costs 2.9% of a
    // core at 30 fps against 7.2% at 60, which is a large fraction of the
    // reason to reach for it at all. Most of that saving is AppKit's
    // display-link dispatch rather than our drawing, which is why halving the
    // rate more than halves the cost.
    const bool halve = m_settings.halfRateOnBattery() && isLowPowerMode();
    m_presenter->setPreferredFps(halve ? 30 : 60);
}

bool App::frameIsAtRest(const FrameState &state) const
{
    if (state.presence > 0.0f)
        return false;
    for (int k = 0; k < state.bandCount; ++k) {
        if (state.bars[k] > 0.001f || state.peaks[k] > 0.001f)
            return false;
    }
    return true;
}

void App::onPermissionChanged(int state)
{
    QMetaObject::invokeMethod(m_worker, &DspWorker::setPermissionState, Qt::QueuedConnection,
                              state);

    if (static_cast<PermissionState>(state) == PermissionState::LikelyDenied) {
        qWarning("hyperlight: system audio access appears to be denied. "
                 "Open the menu bar item to grant it.");
    }
}

void App::runVerifyTick()
{
    FrameState state;
    if (!m_worker->frames()->tryRead(state))
        return;

    int loudest = 0;
    for (int k = 1; k < state.bandCount; ++k) {
        if (state.bars[k] > state.bars[loudest])
            loudest = k;
    }

    qInfo("rms %7.1f dB   agc %5.1f dB   presence %4.2f   loudest band %2d (%.2f)   "
          "callbacks %llu   overruns %llu",
          static_cast<double>(state.rmsDb), static_cast<double>(state.agcDb),
          static_cast<double>(state.presence), loudest,
          static_cast<double>(state.bars[loudest]),
          static_cast<unsigned long long>(m_audio->source()->callbackCount()),
          static_cast<unsigned long long>(m_audio->source()->overrunCount()));

    if (++m_verifyTicks >= static_cast<int>(kVerifySeconds * 4)) {
        qInfo("capture: %s", qPrintable(m_audio->source()->statusDetail()));
        QCoreApplication::quit();
    }
}

} // namespace hyperlight
