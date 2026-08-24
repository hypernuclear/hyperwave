#include "app/App.h"

#include <algorithm>

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>

#include "app/DebugHud.h"
#include "app/Shell.h"
#include "render/Palette.h"

#ifdef Q_OS_MACOS
#include "platform/MacNotchPresenter.h"
#endif

namespace hyperlight {

namespace {

constexpr int kCompactBands = 14;

// A couple of settled frames before a clock is allowed to sleep, so a momentary
// gap between tracks does not stop and restart it.
constexpr int kAtRestFramesBeforeSleep = 4;

constexpr qreal kVerifySeconds = 5.0;

} // namespace

App::App(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_settings(this)
    , m_audio(std::make_unique<AudioController>())
    , m_nowPlaying(std::make_unique<NowPlaying>())
{
    m_dspThread.setObjectName(QStringLiteral("hyperlight.dsp"));
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
    surface->isNotch = isNotch;
    Surface *s = surface.get();
    m_surfaces.push_back(std::move(surface));

    BarPresenter *p = s->presenter.get();
    connect(p, &BarPresenter::frameDue, this, [this, s] { onFrameDue(*s); });
    connect(p, &BarPresenter::geometryChanged, this, [this, s] { onGeometryChanged(*s); });

    if (isNotch) {
        m_notchSurface = s;
        auto *notch = static_cast<MacNotchPresenter *>(p);
        connect(notch, &MacNotchPresenter::transportRequested, this, [this](int command) {
            m_nowPlaying->sendCommand(static_cast<TransportCommand>(command));
        });
        // The panel needs its own route to the menu: it exists for displays
        // where the status item has been crowded off the bar entirely.
        connect(notch, &BarPresenter::menuActionTriggered, this, &App::onMenuAction);
        connect(notch, &BarPresenter::geometryChanged, this, &App::refreshNowPlaying);
    }
    if (!isNotch) {
        // The status item carries the menu; the notch panel expands instead.
        m_menuSurface = s;
        connect(p, &BarPresenter::menuAboutToShow, this, &App::refreshMenu);
        connect(p, &BarPresenter::menuActionTriggered, this, &App::onMenuAction);
    }

    onGeometryChanged(*s);
    p->startClock();
}

bool App::start()
{
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
    QMetaObject::invokeMethod(m_worker, &DspWorker::setFormat, Qt::QueuedConnection,
                              m_audio->source()->sampleRate(),
                              m_audio->source()->formatEpoch());
    QMetaObject::invokeMethod(m_worker, &DspWorker::start, Qt::QueuedConnection);

    if (m_options.verifyAudio) {
        auto *timer = new QTimer(this);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, &App::runVerifyTick);
        timer->start();
        return true;
    }

    if (!m_options.windowed) {
        addSurface(BarPresenter::create(this), false);
        if (m_surfaces.empty()) {
            qCritical("hyperlight: could not create a menu bar item");
            return false;
        }

#ifdef Q_OS_MACOS
        // The notch panel is additional, never a replacement. The status item
        // is what people expect to find and what they can drag; the panel is
        // insurance against it being crowded off a notched display entirely.
        MacNotchPresenter::setFakeNotch(m_options.fakeNotch);
        if (MacNotchPresenter::isAvailable())
            addSurface(std::make_unique<MacNotchPresenter>(this), true);
#endif

        applyFrameRate();
        refreshMenu();
        m_nowPlaying->setEnabled(m_settings.nowPlayingEnabled());
        connect(m_nowPlaying.get(), &NowPlaying::infoChanged, this,
                [this](const NowPlayingInfo &) {
                    refreshNowPlaying();
                    refreshMenu();
                });
        refreshNowPlaying();
    }

    if (m_options.windowed || m_options.hud) {
        m_hud = std::make_unique<DebugHud>();
        m_hud->setSource(m_audio->source());
        m_hud->show();

        if (m_options.windowed) {
            // No presenter, so nothing is driving a clock. A plain timer is fine
            // here: this path exists for iterating on the renderer.
            auto surface = std::make_unique<Surface>();
            Surface *s = surface.get();
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
        const bool compact = m_settings.compact() || surface.presenter->prefersCompact();
        surface.style.bandCount = compact ? kCompactBands : dsp::kBandCount;
    }

    // The menu bar's own foreground colour, for the presets that follow the
    // system rather than carrying their own palette.
    const QColor foreground = QGuiApplication::palette().color(QPalette::WindowText);
    Palette::applyTo(surface.style, m_settings.gradientId(), foreground);

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
        BarRenderer::pixelSize(surface.style, surface.heightPt, surface.dpr);
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
    surface.dpr = surface.presenter->devicePixelRatio();
    rebuildStyle(surface);
    surface.presenter->setWidthPt(surface.style.stripWidthPt());
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

    surface.renderer.render(state, surface.style, surface.heightPt, surface.dpr, target);

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
        qInfo("hyperlight: wrote %s (%dx%d px, %.1f x %.1f pt @ %.0fx)",
              qPrintable(m_options.shotPath), target.width(), target.height(),
              surface.style.stripWidthPt(), surface.heightPt, surface.dpr);
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
    refreshNowPlaying();
}
void App::setNotchTabVisible(bool visible)
{
    if (!m_notchSurface)
        return;
    auto *notch = static_cast<MacNotchPresenter *>(m_notchSurface->presenter.get());
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
        qWarning("hyperlight: system audio access appears to be denied. "
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
            // Now it can honestly say "playing", because it was measured. A
            // scriptable app that is paused still overrides this -- it knows
            // better than a level meter does.
            QString detail = QStringLiteral("playing");
            if (m_settings.nowPlayingEnabled() && NowPlaying::isSupported(process.bundleId)
                && m_nowPlaying->bundleId() == process.bundleId
                && m_nowPlaying->info().valid && !m_nowPlaying->info().playing) {
                detail = QStringLiteral("paused");
            }
            playing.append(MenuItem::action(QString(), process.name)
                               .withIcon(process.bundleId)
                               .withDetail(detail)
                               .withEnabled(false));
        }
        menu.append(MenuItem::submenu(QStringLiteral("Playing"), playing)
                        .withDetail(QString::number(audible.size())));
    }
    // --- now playing, where we can actually read it ---
    const NowPlayingInfo &np = m_nowPlaying->info();
    if (m_settings.nowPlayingEnabled() && np.valid) {
        menu.append(MenuItem::separator());
        menu.append(MenuItem::action(QString(), np.title)
                        .withDetail(np.artist)
                        .withIcon(np.bundleId)
                        .withEnabled(false));
        menu.append(MenuItem::action(QLatin1String(MenuAction::kTransportPrevious),
                                     QStringLiteral("Previous")));
        menu.append(MenuItem::action(QLatin1String(MenuAction::kTransportPlayPause),
                                     np.playing ? QStringLiteral("Pause")
                                                : QStringLiteral("Play")));
        menu.append(
            MenuItem::action(QLatin1String(MenuAction::kTransportNext), QStringLiteral("Next")));
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
    gradients.append(MenuItem::action(QLatin1String(MenuAction::kToggleCompact),
                                      QStringLiteral("Compact strip"))
                         .withCheck(m_settings.compact()));
    if (m_notchSurface) {
        gradients.append(MenuItem::action(QLatin1String(MenuAction::kToggleAutoHide),
                                          QStringLiteral("Hide notch panel when silent"))
                             .withCheck(m_settings.notchAutoHide()));
    }
    menu.append(MenuItem::submenu(QStringLiteral("Appearance"), gradients));
    menu.append(MenuItem::action(QLatin1String(MenuAction::kToggleNowPlaying),
                                 QStringLiteral("Track info and controls"))
                    .withCheck(m_settings.nowPlayingEnabled()));
    // Always offered, never conditional on an accusation.
    //
    // This used to appear only when the app had concluded it was being denied,
    // which was worse than useless: the conclusion was unreliable, so it turned
    // up on a working install and stayed hidden on the one occasion someone
    // would want it. One permanent row costs nothing and is always correct.
    menu.append(MenuItem::separator());
    menu.append(MenuItem::action(QLatin1String(MenuAction::kRequestPermission),
                                 QStringLiteral("System Audio Access…")));
    if (m_audio->needsRestartToRecover()) {
        menu.append(MenuItem::action(QLatin1String(MenuAction::kRestart),
                                     QStringLiteral("Restart hyperlight")));
    }
    menu.append(MenuItem::separator());
    menu.append(
        MenuItem::action(QLatin1String(MenuAction::kAbout), QStringLiteral("About hyperlight")));
    menu.append(
        MenuItem::action(QLatin1String(MenuAction::kQuit), QStringLiteral("Quit hyperlight")));
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
        auto *notch = static_cast<MacNotchPresenter *>(m_notchSurface->presenter.get());
        notch->setDevices(m_audio->source()->audioDevices(),
                          m_audio->source()->sourceDeviceUid());
    }
}

void App::refreshNowPlaying()
{
    // Which app to ask about.
    //
    // Deliberately not "whichever process reports IsRunningOutput": that
    // property means a process holds an open output stream, not that it is
    // making a sound, so it picks the 3D editor that has had an idle audio
    // context open since launch. Asking only the apps we can actually read, in
    // a fixed order, is less clever and more often right.
    QString bundleId;
    QString fallbackName;
    for (const AudioProcess &process : m_audio->source()->audioProcesses()) {
        if (!process.isNamed())
            continue;
        if (bundleId.isEmpty() && NowPlaying::isSupported(process.bundleId)) {
            bundleId = process.bundleId;
            fallbackName = process.name;
        } else if (fallbackName.isEmpty() && process.playingOutput) {
            // Nothing scriptable is running, so the panel falls back to the
            // name of whatever has audio open. That is all we can honestly say.
            fallbackName = process.name;
        }
    }
    // Unconditional, and this is the point.
    //
    // This used to sit behind an early return when there was no notch panel, so
    // on a Mac without one the whole feature was dead: NowPlaying was never
    // pointed at an app, never polled, and the "Track info and controls" switch
    // toggled a setting that nothing read. The panel is one consumer of this,
    // not the reason for it -- the menu is the other.
    m_nowPlaying->setBundleId(bundleId);
    if (m_notchSurface) {
        auto *notch = static_cast<MacNotchPresenter *>(m_notchSurface->presenter.get());
        notch->setNowPlaying(m_nowPlaying->info(), fallbackName);
    }
}
void App::onMenuAction(const QString &actionId)
{
    const QLatin1String sourcePrefix(MenuAction::kSourcePrefix);
    const QLatin1String gradientPrefix(MenuAction::kGradientPrefix);

    if (actionId.startsWith(sourcePrefix)) {
        const QString uid = actionId.mid(sourcePrefix.size());
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
    if (actionId == QLatin1String(MenuAction::kTransportPlayPause)) {
        m_nowPlaying->sendCommand(TransportCommand::PlayPause);
        return;
    }
    if (actionId == QLatin1String(MenuAction::kTransportNext)) {
        m_nowPlaying->sendCommand(TransportCommand::Next);
        return;
    }
    if (actionId == QLatin1String(MenuAction::kTransportPrevious)) {
        m_nowPlaying->sendCommand(TransportCommand::Previous);
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleAutoHide)) {
        m_settings.setNotchAutoHide(!m_settings.notchAutoHide());
        setNotchTabVisible(true);
        onAudioBecameActive();
        return;
    }
    if (actionId == QLatin1String(MenuAction::kToggleNowPlaying)) {
        const bool on = !m_settings.nowPlayingEnabled();
        m_settings.setNowPlayingEnabled(on);
        // Turning this on is what triggers the Automation prompt, and only
        // then: the first run is otherwise a single clean permission question.
        m_nowPlaying->setEnabled(on);
        refreshNowPlaying();
        refreshMenu();
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
    box.setWindowTitle(QStringLiteral("hyperlight"));
    box.setText(QStringLiteral("<b>hyperlight</b> %1").arg(QStringLiteral(HYPERLIGHT_VERSION)));
    box.setInformativeText(QStringLiteral("A system audio visualiser that lives in the menu bar."
                                          "\n\nCapture: %1\nRate: %2 Hz\n\n"
                                          "© 2026 Hypernuclear LLC")
                               .arg(m_audio->source()->statusDetail())
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
