#include <QSysInfo>

#include "app/App.h"

#include "analytics/Analytics.h"

#include <algorithm>

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPalette>
#include <QVBoxLayout>
#include <QTimer>

#include "app/DebugHud.h"
#include "app/Shell.h"
#include "render/Palette.h"

#include "platform/PanelPresenter.h"
#ifdef Q_OS_MACOS
#include "platform/MacNotchPresenter.h"
#endif

namespace hyperwave {

namespace {

constexpr int kCompactBands = 14;

// A couple of settled frames before a clock is allowed to sleep, so a momentary
// gap between tracks does not stop and restart it.
constexpr int kAtRestFramesBeforeSleep = 4;

constexpr qreal kVerifySeconds = 5.0;

} // namespace

std::unique_ptr<Visualiser> App::makeVisualiser(const QString &id)
{
    if (id == QLatin1String("ridge"))
        return std::make_unique<RidgeRenderer>();
    if (id == QLatin1String("lines"))
        return std::make_unique<LineRenderer>();
    if (id == QLatin1String("mirror"))
        return std::make_unique<MirrorRenderer>();
    if (id == QLatin1String("ring"))
        return std::make_unique<RingRenderer>();
    if (id == QLatin1String("strands")) {
        // A machine with no usable GPU gets the bars rather than an empty
        // status item, which is indistinguishable from a crash.
        if (StrandRenderer::isAvailable())
            return std::make_unique<StrandRenderer>();
        qWarning("hyperwave: no GPU available for the strands look, using bars");
    }
    // Bars are the default and the fallback. An unknown id in a settings file
    // -- a downgrade, a typo, a look that was removed -- must produce the
    // ordinary visualiser rather than an empty menu bar item, which is
    // indistinguishable from a crash.
    return std::make_unique<BarRenderer>();
}

RenderStyle::Bloom App::defaultBloomFor(const QString &id)
{
    RenderStyle::Bloom bloom;
    // The lines are strokes of light and the ring is a lit rim; both are
    // drawing something that emits rather than something that reflects, and
    // both look wrong without it. The rest are objects with edges.
    if (id == QLatin1String("lines") || id == QLatin1String("ring")) {
        bloom.enabled = true;
    }
    return bloom;
}

QString App::activeVizId() const
{
    return m_options.vizStyle.isEmpty() ? m_settings.vizStyle() : m_options.vizStyle;
}

RenderStyle::Bloom App::bloomFor(const QString &id) const
{
    const auto it = m_bloomTuning.constFind(id);
    return it != m_bloomTuning.constEnd() ? *it : defaultBloomFor(id);
}

void App::refreshSurface(Surface &surface)
{
    if (surface.presenter) {
        onGeometryChanged(surface);
        return;
    }
    // The preview window has no presenter, so it never gets a geometry change
    // and has to be re-styled, re-sized and repainted by hand.
    rebuildStyle(surface);
    ensureBuffers(surface);
    surface.lastPresentedSeq = 0;
    onFrameDue(surface);
}

void App::applyVisualiserTuning(VisualiserControls *origin)
{
    for (auto &surface : m_surfaces)
        refreshSurface(*surface);
    onAudioBecameActive();
    // Whichever panel did not originate the change has to be told, and the one
    // that did must not be: seeding it rewrites the widget the user is
    // currently editing.
    const auto seed = [this, origin](VisualiserControls *panel) {
        if (!panel || panel == origin)
            return;
        panel->setStrands(m_strandTuning);
        panel->setRidge(m_ridgeTuning);
        panel->setLines(m_lineTuning);
        panel->setMirror(m_mirrorTuning);
        panel->setRing(m_ringTuning);
        panel->setBloom(bloomFor(activeVizId()));
    };
    seed(m_hud ? m_hud->controls() : nullptr);
    seed(m_settingsControls);
}
void App::connectControls(VisualiserControls *controls)
{
    if (!controls)
        return;
    controls->setVisualiser(m_options.vizStyle.isEmpty() ? m_settings.vizStyle()
                                                         : m_options.vizStyle);
    controls->setPreviewScale(m_options.previewScale);
    controls->setStrands(m_strandTuning);
    controls->setRidge(m_ridgeTuning);
    controls->setLines(m_lineTuning);
    controls->setMirror(m_mirrorTuning);
    controls->setRing(m_ringTuning);
    controls->setBloom(bloomFor(activeVizId()));
    connect(controls, &VisualiserControls::visualiserRequested, this, [this](const QString &id) {
        m_options.vizStyle = id;
        m_settings.setVizStyle(id);
        for (auto &surface : m_surfaces) {
            surface->renderer = makeVisualiser(id);
            refreshSurface(*surface);
        }
        onAudioBecameActive();
        if (m_hud && m_hud->controls())
            m_hud->controls()->setVisualiser(id);
        if (m_settingsControls)
            m_settingsControls->setVisualiser(id);
        // The glow panel is showing the previous look's settings until told
        // otherwise, because glow is per look now.
        if (m_hud && m_hud->controls())
            m_hud->controls()->setBloom(bloomFor(id));
        if (m_settingsControls)
            m_settingsControls->setBloom(bloomFor(id));
    });
    connect(controls, &VisualiserControls::previewScaleRequested, this, [this](qreal scale) {
        m_options.previewScale = scale;
        for (auto &surface : m_surfaces) {
            if (!surface->presenter)
                surface->dpr = 2.0 * scale;
            refreshSurface(*surface);
        }
        onAudioBecameActive();
    });
    connect(controls, &VisualiserControls::strandsRequested, this,
            [this, controls](const RenderStyle::Strands &strands) {
                m_strandTuning = strands;
                m_settings.setStrands(strands);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::ridgeRequested, this,
            [this, controls](const RenderStyle::Ridge &ridge) {
                m_ridgeTuning = ridge;
                m_settings.setRidge(ridge);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::linesRequested, this,
            [this, controls](const RenderStyle::Lines &lines) {
                m_lineTuning = lines;
                m_settings.setLines(lines);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::mirrorRequested, this,
            [this, controls](const RenderStyle::Mirror &mirror) {
                m_mirrorTuning = mirror;
                m_settings.setMirror(mirror);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::ringRequested, this,
            [this, controls](const RenderStyle::Ring &ring) {
                m_ringTuning = ring;
                m_settings.setRing(ring);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::bloomRequested, this,
            [this, controls](const RenderStyle::Bloom &bloom) {
                const QString id = activeVizId();
                m_bloomTuning.insert(id, bloom);
                m_settings.setBloom(id, bloom);
                applyVisualiserTuning(controls);
            });
    connect(controls, &VisualiserControls::resetRequested, this, [this] {
        m_settings.resetVisualiserParameters();
        const RenderStyle defaults;
        m_strandTuning = defaults.strands;
        m_ridgeTuning = defaults.ridge;
        m_lineTuning = defaults.lines;
        m_mirrorTuning = defaults.mirror;
        m_ringTuning = defaults.ring;
        m_bloomTuning.clear();
        applyVisualiserTuning(nullptr);   // a reset must reach every panel
    });
}
void App::showSettings()
{
    if (!m_settingsWindow) {
        m_settingsWindow = std::make_unique<QWidget>();
        m_settingsWindow->setWindowTitle(QStringLiteral("hyperwave settings"));
        auto *layout = new QVBoxLayout(m_settingsWindow.get());
        layout->setContentsMargins(18, 18, 18, 18);
        // No magnification here: it exists to inspect a renderer at a size the
        // menu bar cannot show, which is a development concern rather than
        // something to offer alongside the look itself.
        m_settingsControls =
            new VisualiserControls({false, true}, m_settingsWindow.get());
        layout->addWidget(m_settingsControls);
        layout->addStretch();
        connectControls(m_settingsControls);
    }
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}
App::App(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_settings(this)
    , m_audio(std::make_unique<AudioController>())
{
    m_dspThread.setObjectName(QStringLiteral("hyperwave.dsp"));
    // Whatever was tuned last time, before anything draws a frame.
    m_strandTuning = m_settings.strands();
    m_ridgeTuning = m_settings.ridge();
    m_lineTuning = m_settings.lines();
    m_mirrorTuning = m_settings.mirror();
    m_ringTuning = m_settings.ring();
    for (const QString &id : {QStringLiteral("bars"), QStringLiteral("mirror"),
                              QStringLiteral("lines"), QStringLiteral("ring"),
                              QStringLiteral("strands"), QStringLiteral("ridge")}) {
        m_bloomTuning.insert(id, m_settings.bloom(id, defaultBloomFor(id)));
    }

    // The environment variable seeds the tuning rather than being consulted on
    // every restyle, so the HUD's slider can move away from it afterwards
    // instead of being dragged back on the next geometry change.
    if (qEnvironmentVariableIsSet("HYPERWAVE_STRANDS")) {
        bool ok = false;
        const int count = qEnvironmentVariableIntValue("HYPERWAVE_STRANDS", &ok);
        if (ok && count > 0)
            m_strandTuning.count = count;
    }
}

App::~App()
{
    // Ordering matters and it is the reverse of construction.
    //
    // The clocks go first so nothing tries to render into a buffer that is
    // about to be freed; then capture stops, which joins the HAL IO thread and
    // guarantees the ring's producer is gone; only then does the DSP thread
    // wind down and take the consumer with it. Tearing the DSP down first
    // leaves a live producer writing into a ring whose reader has been
    // destroyed, which does not crash immediately and so gets found by a user
    // rather than by us.
    for (auto &surface : m_surfaces)
        surface->presenter->stopClock();
    if (m_audio)
        m_audio->stop();
    if (m_dspThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, &DspWorker::stop, Qt::BlockingQueuedConnection);
        m_dspThread.quit();
        m_dspThread.wait();
    }
    for (auto &surface : m_surfaces)
        surface->presenter->remove();
}

void App::addSurface(std::unique_ptr<BarPresenter> presenter, bool isNotch)
{
    if (!presenter || !presenter->install())
        return;

    auto surface = std::make_unique<Surface>();
    surface->presenter = std::move(presenter);
    surface->renderer = makeVisualiser(m_options.vizStyle.isEmpty() ? m_settings.vizStyle()
                                                                    : m_options.vizStyle);
    surface->isNotch = isNotch;
    Surface *s = surface.get();
    m_surfaces.push_back(std::move(surface));

    BarPresenter *p = s->presenter.get();
    connect(p, &BarPresenter::frameDue, this, [this, s] { onFrameDue(*s); });
    connect(p, &BarPresenter::geometryChanged, this, [this, s] { onGeometryChanged(*s); });
    // Every surface reports this, so the review still happens when the surface
    // that would have reported it is the one being taken away.
    connect(p, &BarPresenter::screenConfigurationChanged, this, &App::scheduleSurfaceReview);

    if (isNotch) {
        m_notchSurface = s;
        // The panel needs its own route to the menu: it exists for displays
        // where the status item has been crowded off the bar entirely.
        connect(p, &BarPresenter::menuActionTriggered, this, &App::onMenuAction);
        // The panel rebuilds the menu before showing it, same as the status
        // item. Without this the signal is emitted into nothing.
        connect(p, &BarPresenter::menuAboutToShow, this, &App::refreshMenu);
    }
    if (!isNotch) {
        // The status item carries the menu; the notch panel expands instead.
        m_barSurface = s;
        connect(p, &BarPresenter::menuAboutToShow, this, &App::refreshMenu);
        connect(p, &BarPresenter::menuActionTriggered, this, &App::onMenuAction);
    }

    onGeometryChanged(*s);
    p->startClock();
}

bool App::start()
{
    // Null in a build with no app key, which is every local build. Created
    // before anything that records, so the launch event is not lost.
    m_analytics = Analytics::create(this);
    if (m_analytics && m_analytics->enabled()) {
        m_analytics->property(Prop::kVersion, QStringLiteral(HYPERWAVE_VERSION));
        m_analytics->property(Prop::kOs, QSysInfo::productType());
        m_analytics->property(Prop::kOsVersion, QSysInfo::productVersion());
        m_analytics->property(Prop::kLook, m_settings.vizStyle());
        m_analytics->property(Prop::kGradient, m_settings.gradientId());
        m_analytics->event(Ev::kLaunch);
    }

    // Null on every platform without an updater, and on any build made without
    // a signing key. The menu asks before it offers the entry.
    m_updater = AppUpdater::create(this);
    if (m_updater) {
        // The entry greys itself out while a check is in flight, so the menu
        // has to be rebuilt when that changes rather than only when the audio
        // does.
        connect(m_updater, &AppUpdater::canCheckForUpdatesChanged, this,
                [this](bool) { refreshMenu(); });
    }
    if (m_options.sourceBundleId == QLatin1String("list")) {
        // Print what the audio system will admit to and exit. Cheaper than
        // guessing bundle ids, and it doubles as a check that the process probe
        // is returning un-redacted names.
        for (const AudioDevice &device : m_audio->source()->audioDevices()) {
            qInfo("%-8s %-34s %s", device.isDefaultOutput ? "OUTPUT" : "-",
                  qPrintable(device.name), qPrintable(device.uid));
        }
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return true;
    }

    m_worker = new DspWorker;
    m_worker->setRing(m_audio->source()->ring());
    m_worker->setFakeAudio(m_options.fakeAudio);
    m_worker->setInputRing(m_audio->source()->inputRing());
    m_worker->moveToThread(&m_dspThread);
    connect(&m_dspThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_audio.get(), &AudioController::formatChanged, m_worker, &DspWorker::setFormat);
    connect(m_audio.get(), &AudioController::permissionChanged, this, &App::onPermissionChanged);
    connect(m_worker, &DspWorker::audioBecameActive, this, &App::onAudioBecameActive);
    connect(m_worker, &DspWorker::audioBecameSilent, this, &App::onAudioBecameSilent);
    connect(m_worker, &DspWorker::silenceRunChanged, m_audio.get(),
            &AudioController::noteSilenceRun);

    m_dspThread.start();

    // The source is chosen before capture starts, so the first tap is built for
    // the right processes rather than being torn down and rebuilt immediately.
    const QString source = m_options.sourceBundleId.isEmpty() ? m_settings.sourceDeviceUid()
                                                              : m_options.sourceBundleId;
    if (!source.isEmpty())
        m_audio->source()->setSourceDeviceUid(source);

    m_audio->start();
    // Restore the input mix after capture is up, so a failure to start it
    // cannot take the output tap down with it.
    if (m_settings.inputEnabled() && !m_audio->source()->setInputEnabled(true))
        m_settings.setInputEnabled(false);
    // A synthetic source has no device to ask, so give the analyser a rate to
    // configure itself against or it never builds its band tables and the whole
    // chain sits idle producing nothing.
    if (m_options.fakeAudio) {
        QMetaObject::invokeMethod(m_worker, &DspWorker::setFormat, Qt::QueuedConnection, 48000.0,
                                  1u);
    } else {
        QMetaObject::invokeMethod(m_worker, &DspWorker::setFormat, Qt::QueuedConnection,
                                  m_audio->source()->sampleRate(),
                                  m_audio->source()->formatEpoch());
    }
    QMetaObject::invokeMethod(m_worker, &DspWorker::start, Qt::QueuedConnection);

    // The "Playing" list, printed on a timer, from the same call the menu uses.
    //
    // Worth its few lines: every wrong answer this list has given -- an idle 3D
    // editor listed as playing, a browser that would not appear at all -- was
    // found by someone opening a menu and disagreeing with it, which is a slow
    // and unreliable way to test a measurement. This makes the same question
    // answerable in one command with no clicking.
    if (qEnvironmentVariableIsSet("HYPERWAVE_METER_DEBUG")) {
        auto *timer = new QTimer(this);
        timer->setInterval(2000);
        connect(timer, &QTimer::timeout, this, [this] {
            AudioProcessList processes = m_audio->source()->audioProcesses();
            processes.removeIf([](const AudioProcess &p) { return !p.isNamed() || !p.audible; });
            std::sort(processes.begin(), processes.end(),
                      [](const AudioProcess &a, const AudioProcess &b) {
                          return a.levelDb > b.levelDb;
                      });
            QStringList playing;
            for (const AudioProcess &p : processes)
                playing << QStringLiteral("%1 %2 dB").arg(p.name).arg(p.levelDb, 0, 'f', 1);
            qInfo("hyperwave: playing: %s",
                  playing.isEmpty() ? "(nothing)" : qPrintable(playing.join(QStringLiteral(", "))));
        });
        timer->start();
    }

    if (m_options.verifyAudio) {
        auto *timer = new QTimer(this);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, &App::runVerifyTick);
        timer->start();
        return true;
    }

    if (!m_options.windowed) {
#ifdef Q_OS_MACOS
        // Read before the first review, since it decides whether there is a
        // notch to prefer.
        MacNotchPresenter::setFakeNotch(m_options.fakeNotch);
#endif
        // One surface, chosen here and re-chosen whenever the display changes
        // or the preference does. The panel and the status item are alternatives
        // rather than companions: they draw the same strip, and running both put
        // two of them in the same corner of the same screen.
        reviewSurfaces();
        if (m_surfaces.empty()) {
            qCritical("hyperwave: could not create a menu bar item");
            return false;
        }

    }

    if (m_options.windowed || m_options.hud) {
        m_hud = std::make_unique<DebugHud>();
        m_hud->setSource(m_audio->source());
        connectControls(m_hud->controls());
        m_hud->show();

        if (m_options.windowed) {
            // No presenter, so nothing is driving a clock. A plain timer is fine
            // here: this path exists for iterating on the renderer.
            auto surface = std::make_unique<Surface>();
            Surface *s = surface.get();
            // This path does not go through addSurface(), so it has to say
            // which look it is drawing itself. Forgetting leaves the renderer
            // null and the first frame dereferences it.
            s->renderer = makeVisualiser(m_options.vizStyle.isEmpty() ? m_settings.vizStyle()
                                                                      : m_options.vizStyle);
            // No presenter here, so onGeometryChanged() never runs and nothing
            // else would apply the preview scale. Without this line
            // --preview-scale silently did nothing in the one mode that exists
            // to look at the visuals.
            s->dpr = 2.0 * m_options.previewScale;
            m_surfaces.push_back(std::move(surface));
            rebuildStyle(*s);
            ensureBuffers(*s);

            auto *timer = new QTimer(this);
            timer->setInterval(16);
            connect(timer, &QTimer::timeout, this, [this, s] { onFrameDue(*s); });
            timer->start();
        }
    }

    return true;
}

void App::rebuildStyle(Surface &surface)
{
    surface.style = RenderStyle{};

    // Live tuning wins over the defaults.
    //
    // rebuildStyle() starts from a fresh RenderStyle every time, so without
    // this the first geometry change -- a display switch, the notch panel
    // expanding, the preview being magnified -- would quietly revert every
    // slider the HUD had touched.
    surface.style.strands = m_strandTuning;
    surface.style.ridge = m_ridgeTuning;
    surface.style.lines = m_lineTuning;
    surface.style.mirror = m_mirrorTuning;
    surface.style.ring = m_ringTuning;
    surface.style.bloom = bloomFor(activeVizId());

    if (surface.isNotch) {
        // The panel is the size of the hole in the display, so the strip fits
        // itself to that rather than the other way round. There is room for the
        // full band count here even when the menu bar is too crowded for it.
        surface.style.bandCount = dsp::kBandCount;
        surface.style.insetTopPt = 2.0;
        surface.style.insetBottomPt = 3.0;

#ifdef Q_OS_MACOS
        auto *notch = static_cast<MacNotchPresenter *>(surface.presenter.get());
        const qreal available = notch->availableWidthPt();
        const int n = surface.style.bandCount;

        // Bars grow to fill whatever width the panel has, rather than staying
        // menu-bar sized and floating in the middle of an expanded panel. With
        // equal bar and gap the arithmetic is one division: n bars and n-1 gaps
        // of the same width fill (2n - 1) slots.
        const qreal slot = available / (2 * n - 1);
        surface.style.barWidthPt = std::clamp<qreal>(slot, 2.0, 7.0);
        surface.style.barGapPt = surface.style.barWidthPt;
        surface.style.peakRadiusPt = std::max<qreal>(1.0, surface.style.barWidthPt * 0.45);

        if (notch->isExpanded()) {
            surface.style.minBarPt = surface.style.barWidthPt;
            surface.style.insetTopPt = 4.0;
            surface.style.insetBottomPt = 5.0;
        }

        const qreal content =
            n * surface.style.barWidthPt + (n - 1) * surface.style.barGapPt;
        const qreal pad = std::max<qreal>(4.0, (available - content) / 2.0);
        surface.style.insetLeftPt = pad;
        surface.style.insetRightPt = pad;
#endif
    } else {
        // The windowed surface has no presenter, and this dereferenced it
        // unconditionally: --windowed segfaulted on launch, before drawing a
        // single frame. It had been broken long enough to be committed to a
        // VS Code configuration and documented in the README as the way to
        // iterate on the visuals.
        const bool compact = m_settings.compact()
                             || (surface.presenter && surface.presenter->prefersCompact());
        surface.style.bandCount = compact ? kCompactBands : dsp::kBandCount;
    }

    // The menu bar's own foreground colour, for the presets that follow the
    // system rather than carrying their own palette.
    const QColor foreground = QGuiApplication::palette().color(QPalette::WindowText);
    // The command line wins for this run, without touching the stored setting:
    // comparing presets should not silently rewrite which one the user picked.
    const QString gradientId = m_options.gradientId.isEmpty() ? m_settings.gradientId()
                                                              : m_options.gradientId;
    Palette::applyTo(surface.style, gradientId, foreground);

    surface.style.idleOpacity = dsp::kIdlePresence;

    // Inside the panel the background is our own black, not the wallpaper, so
    // the halo that rescues legibility over a bright desktop has nothing to do
    // but muddy the bars.
    if (surface.isNotch)
        surface.style.drawShadow = false;
}

void App::ensureBuffers(Surface &surface)
{
    const QSize wanted =
        surface.renderer->pixelSize(surface.style, surface.heightPt, surface.dpr);
    for (QImage &buffer : surface.buffers) {
        if (buffer.size() != wanted
            || buffer.format() != QImage::Format_ARGB32_Premultiplied) {
            buffer = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        }
    }
}

void App::onGeometryChanged(Surface &surface)
{
    if (!surface.presenter)
        return;
    surface.heightPt = surface.presenter->heightPt();
    surface.dpr = surface.presenter->devicePixelRatio() * m_options.previewScale;
    rebuildStyle(surface);
    // The look decides how much menu bar it needs, not the bar style.
    surface.presenter->setWidthPt(
        surface.renderer->widthPt(surface.style, surface.heightPt));
    ensureBuffers(surface);

    // Draw one frame right now rather than waiting for the clock.
    //
    // The clock sleeps whenever the picture has stopped moving, so a geometry
    // change while nothing is playing -- collapsing the notch panel, most
    // obviously -- would otherwise leave the previous, differently sized image
    // sitting in the layer, clipped by the new bounds. It looked like half the
    // bars had vanished.
    surface.lastPresentedSeq = 0;
    onFrameDue(surface);
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

void App::onFrameDue(Surface &surface)
{
    if (surface.presenter && !surface.presenter->isVisibleOnScreen())
        return;

    FrameState state;
    if (!m_worker->frames()->tryRead(state))
        return;   // the writer kept winning; there will be another in 16 ms

    // On a 120 Hz display the link fires twice per analysis frame. Presenting
    // the same pixels again costs a full recomposite for nothing.
    if (state.seq == surface.lastPresentedSeq)
        return;
    surface.lastPresentedSeq = state.seq;

    // Once the picture has genuinely stopped moving, stop the clock.
    //
    // This is the single biggest lever on idle cost, and the reason is not the
    // drawing: a running CADisplayLink drags AppKit's whole deferred-display
    // machinery through the run loop on every vsync whether or not anything is
    // done with it.
    surface.atRestFrames = frameIsAtRest(state) ? surface.atRestFrames + 1 : 0;
    if (surface.presenter && surface.atRestFrames > kAtRestFramesBeforeSleep) {
        surface.presenter->stopClock();
        surface.atRestFrames = 0;
        // Retract here rather than on the silence edge: the bars and peak dots
        // are still falling at that point, and pulling the tab out from under
        // them mid-descent looks like a glitch.
        if (surface.isNotch)
            setNotchTabVisible(false);
    }

    QImage &target = surface.buffers[surface.nextBuffer];
    surface.nextBuffer = 1 - surface.nextBuffer;
    if (target.isNull())
        return;

    surface.renderer->render(state, surface.style, surface.heightPt, surface.dpr, target);
    // Bloom last, over whatever was drawn. Deliberately not the renderer's job:
    // deciding which pixels are lit and deciding what light does afterwards are
    // different questions, and keeping them apart is what lets one
    // post-process serve four looks and any look added later.
    m_post.apply(target, surface.style.bloom);

    if (surface.presenter)
        surface.presenter->present(target);

    if (m_hud && (&surface == m_surfaces.front().get())) {
        m_hud->setFrame(state);
        m_hud->setPreview(target);
    }

    // Let the ballistics settle before grabbing. A frame taken at startup is
    // all zeros, which makes for a useless reference image.
    if (!m_options.shotPath.isEmpty() && surface.lastPresentedSeq > 120) {
        target.save(m_options.shotPath);
        qInfo("hyperwave: wrote %s (%dx%d px, %.1f x %.1f pt @ %.0fx)",
              qPrintable(m_options.shotPath), target.width(), target.height(),
              surface.renderer->widthPt(surface.style, surface.heightPt), surface.heightPt,
              surface.dpr);
        QCoreApplication::quit();
    }
}

void App::onAudioBecameActive()
{
    for (auto &surface : m_surfaces) {
        if (surface->presenter && !surface->presenter->isClockRunning())
            surface->presenter->startClock();
    }
    setNotchTabVisible(true);
}

void App::scheduleSurfaceReview()
{
    if (m_options.windowed)
        return;
    if (!m_surfaceReview) {
        m_surfaceReview = new QTimer(this);
        m_surfaceReview->setSingleShot(true);
        // NSApplicationDidChangeScreenParametersNotification arrives while the
        // configuration is still settling, and arrives two or three times for
        // one lid movement. Acting on the first builds the panel against
        // geometry that is about to change again, then tears it straight back
        // down -- visible as the tab flickering in and out on every dock.
        //
        // The delay does a second job for the menu toggle. That action is
        // delivered from inside the tracking of a menu the outgoing presenter
        // owns, and swapping surfaces there would free the status item, and the
        // menu, underneath AppKit.
        m_surfaceReview->setInterval(250);
        connect(m_surfaceReview, &QTimer::timeout, this, &App::reviewSurfaces);
    }
    // restart, not start: the last notification of a burst is the one whose
    // geometry is worth reading.
    m_surfaceReview->start();
}
bool App::notchPanelWanted() const
{
    return MacNotchPresenter::isAvailable() && m_settings.useNotchPanel();
    return false;
}
void App::reviewSurfaces()
{
    if (m_options.windowed)
        return;
    const bool wantNotch = notchPanelWanted();
    if (wantNotch == (m_notchSurface != nullptr) && wantNotch == (m_barSurface == nullptr))
        return;   // already showing the right one, and only the right one
    // The wanted surface is installed before the unwanted one is taken away.
    // install() can refuse -- a status item can fail to be created, and the
    // panel refuses outright when the housing has gone -- and the order means a
    // refusal leaves the old surface up rather than leaving nothing at all.
    if (wantNotch) {
        if (!m_notchSurface)
            addSurface(std::make_unique<MacNotchPresenter>(this), true);
        if (m_notchSurface) {
            retireSurface(m_barSurface);
            setNotchTabVisible(true);
        }
    } else {
        if (!m_barSurface)
            addSurface(BarPresenter::create(this), false);
        if (m_barSurface)
            retireSurface(m_notchSurface);
    }
    if (!m_barSurface && !m_notchSurface) {
        qCritical("hyperwave: no surface could be installed");
        return;
    }
    applyFrameRate();
    // Neither is part of addSurface(): a new surface arrives with no menu, and
    // the entries that describe the notch are conditional on which surface is
    // up, so the one that stayed is now describing the wrong thing.
    refreshMenu();
    qInfo("hyperwave: drawing in the %s", m_notchSurface ? "notch panel" : "menu bar");
}
void App::retireSurface(Surface *&slot)
{
    if (!slot)
        return;
    const auto it = std::find_if(m_surfaces.begin(), m_surfaces.end(),
                                 [slot](const std::unique_ptr<Surface> &s) {
                                     return s.get() == slot;
                                 });
    slot = nullptr;
    if (it == m_surfaces.end())
        return;
    // Detached now, destroyed later.
    //
    // Destroying it here would be simpler and is wrong: this runs from the
    // menu, and on the notch panel that menu is popped up by the presenter
    // itself, which still touches its own members after the action returns.
    // Moving the unique_ptr is pure C++ and cannot reach AppKit; the destructor
    // that does is left to the next turn of the event loop, by which point
    // nothing is tracking. The surface stays alive until then, so any frame
    // callback still in flight finds a live object.
    m_retiredSurfaces.push_back(std::move(*it));
    m_surfaces.erase(it);
    QTimer::singleShot(0, this, [this] {
        for (auto &surface : m_retiredSurfaces) {
            if (surface->presenter)
                surface->presenter->remove();
        }
        m_retiredSurfaces.clear();
    });
}
void App::setNotchTabVisible(bool visible)
{
    if (!m_notchSurface)
        return;
    auto *notch = static_cast<PanelPresenter *>(m_notchSurface->presenter.get());
    notch->setTabVisible(visible || !m_settings.notchAutoHide());
    Q_UNUSED(visible);
}

void App::onAudioBecameSilent()
{
    // Nothing to do beyond noting the edge. The clocks are stopped by
    // onFrameDue once the picture has actually stopped changing, which is a
    // later moment than this one: presence has only just reached zero and the
    // bars and peak dots are still falling.
}

void App::onPermissionChanged(int state)
{
    QMetaObject::invokeMethod(m_worker, &DspWorker::setPermissionState, Qt::QueuedConnection,
                              state);

    if (static_cast<PermissionState>(state) == PermissionState::LikelyDenied) {
        qWarning("hyperwave: system audio access appears to be denied. "
                 "Open the menu bar item to grant it.");
    }
    refreshMenu();
}

MenuModel App::buildMenu() const
{
    MenuModel menu;
    // Sources as a submenu, not a flat list.
    //
    // Inline, ten output devices pushed everything else off the bottom of the
    // menu; one row that opens on demand keeps the menu the size of the app.
    const QString source = m_audio->source()->sourceDeviceUid();
    const AudioDeviceList devices = m_audio->source()->audioDevices();
    MenuModel sources;
    sources.append(MenuItem::action(QLatin1String(MenuAction::kSourcePrefix),
                                    QStringLiteral("All system audio"))
                       .withCheck(source.isEmpty()));
    if (!devices.isEmpty())
        sources.append(MenuItem::separator());
    QString activeName = QStringLiteral("All system audio");
    for (const AudioDevice &device : devices) {
        sources.append(
            MenuItem::action(QLatin1String(MenuAction::kSourcePrefix) + device.uid, device.name)
                .withCheck(device.uid == source)
                .withDetail(device.isDefaultOutput ? QStringLiteral("output") : QString()));
        if (device.uid == source)
            activeName = device.name;
    }
    // The submenu's own row says what is selected, so the answer is visible
    // without opening it.
    menu.append(MenuItem::submenu(QStringLiteral("Source"), sources).withDetail(activeName));
    // --- what is actually making a sound ---
    //
    // Filtered on measured level, not on kAudioProcessPropertyIsRunningOutput.
    // That property only says a process holds an open output stream, which a 3D
    // editor does for hours while emitting digital silence -- it was reported as
    // "playing" three times before this was measured properly.
    AudioProcessList audible = m_audio->source()->audioProcesses();
    audible.removeIf([](const AudioProcess &p) { return !p.isNamed() || !p.audible; });
    std::sort(audible.begin(), audible.end(), [](const AudioProcess &a, const AudioProcess &b) {
        if (a.levelDb != b.levelDb)
            return a.levelDb > b.levelDb;   // loudest first
        return a.name.localeAwareCompare(b.name) < 0;
    });
    if (!audible.isEmpty()) {
        MenuModel playing;
        for (const AudioProcess &process : audible) {
            // It can honestly say "playing", because it was measured.
            playing.append(MenuItem::action(QString(), process.name)
                               .withIcon(process.bundleId)
                               .withDetail(QStringLiteral("playing"))
                               .withEnabled(false));
        }
        menu.append(MenuItem::submenu(QStringLiteral("Playing"), playing)
                        .withDetail(QString::number(audible.size())));
    }
    menu.append(MenuItem::separator());
    MenuModel gradients;
    const QString activeGradient = m_settings.gradientId();
    for (const GradientPreset &preset : Palette::presets()) {
        gradients.append(
            MenuItem::action(QLatin1String(MenuAction::kGradientPrefix) + preset.id, preset.name)
                .withCheck(preset.id == activeGradient));
    }
    gradients.append(MenuItem::separator());
    const QString activeViz = m_options.vizStyle.isEmpty() ? m_settings.vizStyle()
                                                           : m_options.vizStyle;
    gradients.append(MenuItem::action(QLatin1String(MenuAction::kVizPrefix) + QStringLiteral("bars"),
                                      QStringLiteral("Bars"))
                         .withCheck(activeViz == QLatin1String("bars")
                                    || activeViz.isEmpty()));
    for (const auto &entry : {std::pair{"mirror", "Mirror"}, std::pair{"lines", "Lines"},
                              std::pair{"ring", "Ring"}}) {
        gradients.append(
            MenuItem::action(QLatin1String(MenuAction::kVizPrefix) + QLatin1String(entry.first),
                             QLatin1String(entry.second))
                .withCheck(activeViz == QLatin1String(entry.first)));
    }
    if (StrandRenderer::isAvailable()) {
        gradients.append(
            MenuItem::action(QLatin1String(MenuAction::kVizPrefix) + QStringLiteral("strands"),
                             QStringLiteral("Strands"))
                .withCheck(activeViz == QLatin1String("strands")));
    }
    gradients.append(MenuItem::action(QLatin1String(MenuAction::kVizPrefix) + QStringLiteral("ridge"),
                                      QStringLiteral("Terrain"))
                         .withCheck(activeViz == QLatin1String("ridge")));
    gradients.append(MenuItem::separator());
    gradients.append(MenuItem::action(QLatin1String(MenuAction::kToggleCompact),
                                      QStringLiteral("Compact strip"))
                         .withCheck(m_settings.compact()));
#ifdef Q_OS_MACOS
    // Offered whenever the display has a housing, not only while the panel is
    // up: it is the way back from the menu bar as much as the way to the notch.
    if (MacNotchPresenter::isAvailable()) {
        gradients.append(MenuItem::action(QLatin1String(MenuAction::kToggleNotch),
                                          QStringLiteral("Draw in the notch"))
                             .withCheck(m_settings.useNotchPanel()));
    }
#endif
    if (m_notchSurface) {
        gradients.append(MenuItem::action(QLatin1String(MenuAction::kToggleAutoHide),
                                          QStringLiteral("Hide notch panel when silent"))
                             .withCheck(m_settings.notchAutoHide()));
    }
    menu.append(MenuItem::submenu(QStringLiteral("Appearance"), gradients));
    menu.append(MenuItem::action(QLatin1String(MenuAction::kToggleInput),
                                QStringLiteral("Include microphone input"))
                    .withCheck(m_audio->source()->inputEnabled()));
#ifdef Q_OS_MACOS
    // Always offered, never conditional on an accusation.
    //
    // This used to appear only when the app had concluded it was being denied,
    // which was worse than useless: the conclusion was unreliable, so it turned
    // up on a working install and stayed hidden on the one occasion someone
    // would want it. One permanent row costs nothing and is always correct.
    //
    // macOS only, and that is a different kind of conditional: WASAPI loopback
    // has no permission to grant, so on Windows this row would open nothing
    // and quietly imply a gate that does not exist.
    menu.append(MenuItem::separator());
    menu.append(MenuItem::action(QLatin1String(MenuAction::kRequestPermission),
                                 QStringLiteral("System Audio Access…")));
#endif
    if (m_audio->needsRestartToRecover()) {
        menu.append(MenuItem::action(QLatin1String(MenuAction::kRestart),
                                     QStringLiteral("Restart hyperwave")));
    }
    menu.append(MenuItem::separator());
    menu.append(MenuItem::action(QLatin1String(MenuAction::kSettings),
                                QStringLiteral("Settings…")));
    menu.append(MenuItem::action(QLatin1String(MenuAction::kShowSplash),
                                 QStringLiteral("Show Splash")));
    if (m_analytics) {
        // Absent, not greyed out, in a build that has nothing to send to.
        menu.append(MenuItem::action(QLatin1String(MenuAction::kToggleAnalytics),
                                     QStringLiteral("Send Anonymous Usage Data"))
                        .withCheck(m_analytics->enabled()));
    }
    if (m_updater) {
        // Only when there is something behind it. A build with no updater
        // compiled in -- Linux, or any build without a signing key -- shows no
        // entry at all rather than a greyed-out one that can never be used.
        menu.append(MenuItem::action(QLatin1String(MenuAction::kCheckForUpdates),
                                     QStringLiteral("Check for Updates..."))
                        .withEnabled(m_updater->canCheckForUpdates()));
    }
    menu.append(
        MenuItem::action(QLatin1String(MenuAction::kAbout), QStringLiteral("About hyperwave")));
    menu.append(
        MenuItem::action(QLatin1String(MenuAction::kQuit), QStringLiteral("Quit hyperwave")));
    return menu;
}
void App::refreshMenu()
{
    const MenuModel menu = buildMenu();
    for (auto &surface : m_surfaces) {
        if (surface->presenter)
            surface->presenter->setMenu(menu);
    }
    if (m_notchSurface) {
        auto *notch = static_cast<PanelPresenter *>(m_notchSurface->presenter.get());
        notch->setDevices(m_audio->source()->audioDevices(),
                          m_audio->source()->sourceDeviceUid());
    }
}

void App::onMenuAction(const QString &actionId)
{
    const QLatin1String sourcePrefix(MenuAction::kSourcePrefix);
    const QLatin1String gradientPrefix(MenuAction::kGradientPrefix);

    if (actionId.startsWith(sourcePrefix)) {
        const QString uid = actionId.mid(sourcePrefix.size());
        if (m_analytics) {
            // WHETHER a specific device was chosen, never which one. A device
            // name is the user's business and does not leave the machine.
            m_analytics->event(Ev::kSourcePicked,
                               {{QStringLiteral("specific"), !uid.isEmpty()}});
        }
        m_settings.setSourceDeviceUid(uid);
        m_audio->source()->setSourceDeviceUid(uid);
        // The tap was torn down and rebuilt, so the analysis is looking at a
        // stale rolling buffer at a possibly different rate.
        QMetaObject::invokeMethod(m_worker, &DspWorker::setFormat, Qt::QueuedConnection,
                                  m_audio->source()->sampleRate(),
                                  m_audio->source()->formatEpoch());
        onAudioBecameActive();   // the clocks may have gone to sleep
        return;
    }

    if (actionId.startsWith(QLatin1String(MenuAction::kVizPrefix))) {
        const QString id = actionId.mid(QLatin1String(MenuAction::kVizPrefix).size());
        if (m_analytics) {
            m_analytics->event(Ev::kLook, {{QStringLiteral("look"), id}});
            m_analytics->property(Prop::kLook, id);
        }
        m_settings.setVizStyle(id);
        for (auto &surface : m_surfaces) {
            surface->renderer = makeVisualiser(id);
            // The buffers are sized by the renderer, and a look may not want
            // the same footprint, so the geometry has to be recomputed rather
            // than assumed to be unchanged.
            onGeometryChanged(*surface);
        }
        onAudioBecameActive();
        return;
    }
    if (actionId.startsWith(gradientPrefix)) {
        m_settings.setGradientId(actionId.mid(gradientPrefix.size()));
        for (auto &surface : m_surfaces)
            rebuildStyle(*surface);
        onAudioBecameActive();
        return;
    }

    if (actionId == QLatin1String(MenuAction::kToggleCompact)) {
        m_settings.setCompact(!m_settings.compact());
        for (auto &surface : m_surfaces)
            onGeometryChanged(*surface);
        onAudioBecameActive();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleNotch)) {
        m_settings.setUseNotchPanel(!m_settings.useNotchPanel());
        // Scheduled, not immediate. This is running from inside the tracking of
        // a menu that belongs to the presenter about to be taken away.
        scheduleSurfaceReview();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleAutoHide)) {
        m_settings.setNotchAutoHide(!m_settings.notchAutoHide());
        setNotchTabVisible(true);
        onAudioBecameActive();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kRequestPermission)) {
        m_audio->requestPermission();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kRestart)) {
        relaunch();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleInput)) {
        const bool want = !m_audio->source()->inputEnabled();
        // Ask the source first and record what actually happened. Starting can
        // fail -- no input device, no channels, a refused IO proc -- and a menu
        // that ticks itself regardless would be lying about what the app is
        // listening to.
        const bool ok = m_audio->source()->setInputEnabled(want);
        m_settings.setInputEnabled(want && ok);
        if (want && !ok)
            qWarning("hyperwave: could not start input capture");
        onAudioBecameActive();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kSettings)) {
        showSettings();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleAnalytics)) {
        if (m_analytics)
            m_analytics->setEnabled(!m_analytics->enabled());
        refreshMenu();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kShowSplash)) {
        Q_EMIT splashRequested();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kCheckForUpdates)) {
        if (m_updater)
            m_updater->checkForUpdates();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kAbout)) {
        showAbout();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kQuit)) {
        QCoreApplication::quit();
        return;
    }
}

void App::applyFrameRate()
{
    // Sixty is what the visuals were designed at and it is the default. Thirty
    // is the lever for battery: measured on macOS 26 the strip costs 2.9% of a
    // core at 30 fps against 7.2% at 60. Most of that saving is AppKit's
    // display-link dispatch rather than the drawing, which is why halving the
    // rate more than halves the cost.
    const bool halve = m_settings.halfRateOnBattery() && isLowPowerMode();
    for (auto &surface : m_surfaces) {
        if (surface->presenter)
            surface->presenter->setPreferredFps(halve ? 30 : 60);
    }
}

void App::showAbout()
{
    QMessageBox box;
    box.setWindowTitle(QStringLiteral("hyperwave"));
    box.setText(QStringLiteral("<b>hyperwave</b> %1").arg(QStringLiteral(HYPERWAVE_VERSION)));
    // Rich text, for the sake of the one link.
    //
    // Which costs two things worth naming. QMessageBox decides plain or rich by
    // sniffing the string, so adding an anchor silently reformats the whole
    // block: the newlines that used to separate these lines stop separating
    // anything and have to be <br>. And the status detail is interpolated into
    // markup now, so it is escaped -- it carries the output device's name, and
    // there is nothing stopping someone owning a pair of headphones with an
    // ampersand in the name.
    //
    // Nothing else is needed to make the link work: QMessageBox builds the
    // informative label with openExternalLinks set and links reachable by
    // mouse, so it opens in the browser on its own.
    box.setInformativeText(QStringLiteral("A system audio visualiser that lives in the menu bar."
                                          "<br><br>Capture: %1<br>Rate: %2 Hz<br><br>"
                                          "<a href=\"https://hypernuclear.com\">hypernuclear.com</a>"
                                          "<br>© 2026 hypernuclear LLC")
                               .arg(m_audio->source()->statusDetail().toHtmlEscaped())
                               .arg(m_audio->source()->sampleRate(), 0, 'f', 0));
    box.setStandardButtons(QMessageBox::Ok);

    // An LSUIElement app has no Dock icon and no application menu, so a dialog
    // opens behind whatever is frontmost unless the app takes focus first.
    activateApp();
    box.raise();
    box.activateWindow();
    box.exec();
}

void App::relaunch()
{
    relaunchApp();
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
          "callbacks %llu   overruns %llu   mic queue %llu",
          static_cast<double>(state.rmsDb), static_cast<double>(state.agcDb),
          static_cast<double>(state.presence), loudest,
          static_cast<double>(state.bars[loudest]),
          static_cast<unsigned long long>(m_audio->source()->callbackCount()),
          static_cast<unsigned long long>(m_audio->source()->overrunCount()),
          static_cast<unsigned long long>(m_audio->source()->inputBacklogFrames()));

    if (++m_verifyTicks >= static_cast<int>(kVerifySeconds * 4)) {
        qInfo("capture: %s", qPrintable(m_audio->source()->statusDetail()));
        QCoreApplication::quit();
    }
}

} // namespace hyperwave
