#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include "app/App.h"
#include "app/Shell.h"
#include "app/WelcomeBridge.h"

#ifdef Q_OS_WIN
#include <cstdio>
#include <windows.h>

namespace {

// Give a GUI-subsystem binary somewhere to print.
//
// WIN32_EXECUTABLE builds a binary the loader starts with no console attached,
// so every qInfo() and every line of --verify-audio output goes nowhere. That
// matters most for exactly the flag you would reach for first on a new
// platform: "it printed nothing" and "it captured nothing" look identical.
//
// Attaching the parent's console makes output appear in the shell that
// launched it, and does nothing at all when there is no such console -- a
// double-clicked app stays windowless rather than flashing one up.
void attachParentConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
}

} // namespace
#endif

using namespace hyperwave;

int main(int argc, char **argv)
{
    // QApplication rather than QGuiApplication: the settings window and the
    // debug HUD are Widgets. The menu bar item itself is native AppKit and
    // wants nothing from Qt at all.
#ifdef Q_OS_WIN
    attachParentConsole();
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hyperwave"));
    app.setOrganizationName(QStringLiteral("Hypernuclear"));
    app.setApplicationVersion(QStringLiteral(HYPERWAVE_VERSION));

    // The status item IS the app. Nothing here should die because a window
    // closed, and most of the time there are no windows at all.
    QApplication::setQuitOnLastWindowClosed(false);

    // Satoshi, the app's one typeface, compiled in from the build's own
    // Fontshare download (CMakeLists.txt). Registered before any QML loads, so
    // every window names "Satoshi" and picks a cut by font.weight.
    for (const QString &cut : QDir(QStringLiteral(":/fonts")).entryList({QStringLiteral("*.otf")})) {
        if (QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + cut) < 0)
            qWarning("hyperwave: could not load font %s", qPrintable(cut));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A system audio visualiser that lives in the menu bar."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption windowedOption(
        QStringLiteral("windowed"),
        QStringLiteral("Render into a window instead of the menu bar, for iterating on the "
                       "visuals without fighting the shell."));
    const QCommandLineOption hudOption(
        QStringLiteral("hud"),
        QStringLiteral("Show the diagnostics window alongside the menu bar item."));
    const QCommandLineOption verifyOption(
        QStringLiteral("verify-audio"),
        QStringLiteral("Print capture levels for five seconds, then exit."));
    const QCommandLineOption sourceOption(
        QStringLiteral("source"),
        QStringLiteral("Listen to one app instead of the whole system mix, by bundle id "
                       "(e.g. com.spotify.client). Use \"list\" to print what is available."),
        QStringLiteral("bundle-id"));
    const QCommandLineOption shotOption(
        QStringLiteral("shot"),
        QStringLiteral("Render one frame to <path> and exit."), QStringLiteral("path"));
    const QCommandLineOption vizOption(
        QStringLiteral("viz"),
        QStringLiteral("Which visualiser to draw: \"bars\" (default) or \"ridge\", a "
                       "spectrogram terrain with frequency across, level up and time "
                       "receding. Overrides the saved preference for this run only."),
        QStringLiteral("style"));

    parser.addOption(windowedOption);
    parser.addOption(hudOption);
    parser.addOption(verifyOption);
    const QCommandLineOption fakeNotchOption(
        QStringLiteral("fake-notch"),
        QStringLiteral("Pretend the display has a notch, for developing the panel on an "
                       "external monitor."));
    parser.addOption(sourceOption);
    parser.addOption(fakeNotchOption);
    parser.addOption(shotOption);
    const QCommandLineOption previewScaleOption(
        QStringLiteral("preview-scale"),
        QStringLiteral("Magnify the windowed and --shot canvas by this factor, for looking at "
                       "a visualiser at a size where its detail is visible. Applied as device "
                       "pixel ratio, so every measurement scales with it."),
        QStringLiteral("factor"));
    const QCommandLineOption gradientOption(
        QStringLiteral("gradient"),
        QStringLiteral("Which colour preset to draw with, by id: \"signature\", \"ember\", "
                       "\"lagoon\", \"rainbow\" or \"mono\". Overrides the setting for "
                       "this run only, which is what makes --shot usable for comparing them."),
        QStringLiteral("id"));
    parser.addOption(vizOption);
    parser.addOption(gradientOption);
    parser.addOption(previewScaleOption);
    parser.process(app);

    App::Options options;
    options.windowed = parser.isSet(windowedOption);
    options.hud = parser.isSet(hudOption);
    options.verifyAudio = parser.isSet(verifyOption);
    options.shotPath = parser.value(shotOption);
    options.sourceBundleId = parser.value(sourceOption);
    options.fakeNotch = parser.isSet(fakeNotchOption);
    options.vizStyle = parser.value(vizOption);
    options.gradientId = parser.value(gradientOption);
    if (parser.isSet(previewScaleOption)) {
        bool ok = false;
        const double scale = parser.value(previewScaleOption).toDouble(&ok);
        if (ok && scale > 0.0)
            options.previewScale = scale;
    }
    options.fakeAudio = !qEnvironmentVariableIsEmpty("HYPERWAVE_FAKE_AUDIO");

    App hyperwaveApp(options);
    if (!hyperwaveApp.start())
        return 1;

    // The brand moment, once at launch.
    //
    // Skipped for the modes that are not a launch at all: --shot and
    // --verify-audio print a thing and exit, and a splash there would either
    // race the exit or be the only thing the run produced. --windowed keeps it,
    // since that IS someone starting the app.
    //
    // The component outlives the window deliberately: the window closes itself
    // after its dwell, and "Show Splash" builds a new one from the same
    // component rather than keeping a hidden window alive for the life of the
    // process.
    QQmlEngine splashEngine;
    QQmlComponent splashComponent(&splashEngine,
                                  QUrl(QStringLiteral("qrc:/qt/qml/hyperwave/qml/Splash.qml")));
    QPointer<QQuickWindow> splashWindow;

    auto showSplash = [&](int dwellMs) {
        if (!splashWindow)
            splashWindow = qobject_cast<QQuickWindow *>(splashComponent.create());
        if (!splashWindow) {
            qWarning("hyperwave: splash failed to load: %s",
                     qPrintable(splashComponent.errorString()));
            return;
        }
        // dwellMs of 0 means "stay until clicked" -- see qml/Splash.qml. The
        // automatic version is the introduction nobody asked for; the manual
        // one is for someone who went looking, and taking it away from them on
        // a timer would be rude.
        splashWindow->setProperty("dwellMs", dwellMs);
        // Centred by hand: a frameless splash has no window manager placement
        // to inherit, so it would otherwise land wherever the platform felt
        // like putting it. Recomputed each time, because the display it should
        // appear on may have changed since.
        if (QScreen *sc = QGuiApplication::primaryScreen()) {
            const QRect a = sc->availableGeometry();
            splashWindow->setPosition(
                a.center() - QPoint(splashWindow->width() / 2, splashWindow->height() / 2));
        }
        QMetaObject::invokeMethod(splashWindow, "appear");

        // A way to see the splash without photographing the screen.
        //
        // screencapture cannot do it: the window is transparent and composited
        // on the GPU, and capturing it by window id returns a blank rectangle.
        // grabWindow() asks the scene graph directly, which is the only thing
        // that produces the real pixels -- and it works headless, so it is also
        // how CI could check the panel still fits after a text change.
        if (qEnvironmentVariableIsSet("HYPERWAVE_SPLASH_SHOT")) {
            const QString path = qEnvironmentVariable("HYPERWAVE_SPLASH_SHOT");
            // After a beat, so the artwork has decoded and the fade has
            // finished; grabbing immediately catches an empty frame.
            QTimer::singleShot(900, splashWindow, [splashWindow, path] {
                if (!splashWindow)
                    return;
                const QImage shot = splashWindow->grabWindow();
                if (shot.save(path))
                    qInfo("hyperwave: wrote %s (%dx%d px)", qPrintable(path), shot.width(),
                          shot.height());
                else
                    qWarning("hyperwave: could not write %s", qPrintable(path));
                qApp->quit();
            });
        }
        // An accessory app is not in the activation order, so a window it opens
        // lands behind whatever the user was looking at.
        activateApp();
    };

    // The welcome window (qml/Welcome.qml): shown by itself after the splash at
    // each launch, until the user unticks "Show at startup" (in the window, or
    // the menu's Show Welcome at Startup), and on demand from the menu's
    // "Welcome to hyperwave...". It carries the version and licence, the
    // capture line, the newsletter sign-up, the studio's other apps, and the
    // Open at Login and usage-data switches. Same engine as the splash.
    WelcomeBridge welcomeBridge;
    welcomeBridge.setAnalytics(hyperwaveApp.analytics());
    welcomeBridge.setSettings(&hyperwaveApp.settings());
    splashEngine.rootContext()->setContextProperty(QStringLiteral("welcome"), &welcomeBridge);
    QQmlComponent welcomeComponent(&splashEngine,
                                   QUrl(QStringLiteral("qrc:/qt/qml/hyperwave/qml/Welcome.qml")));
    QPointer<QQuickWindow> welcomeWindow;
    // `how` is only what analytics counts: "first_run", "startup" or "menu".
    // The window is the same every time.
    auto showWelcome = [&](const QString &how) {
        if (!welcomeWindow) {
            welcomeWindow = qobject_cast<QQuickWindow *>(welcomeComponent.create());
            hideWindowTitle(welcomeWindow);
        }
        if (!welcomeWindow) {
            qWarning("hyperwave: welcome window failed to load: %s",
                     qPrintable(welcomeComponent.errorString()));
            return;
        }
        welcomeBridge.setCaptureStatus(hyperwaveApp.captureStatus());
        // The login item may have changed in System Settings since the window
        // was last up.
        welcomeBridge.refreshOpenAtLogin();
        welcomeWindow->show();
        welcomeWindow->raise();
        welcomeWindow->requestActivate();
        // An accessory app is not in the activation order: see the splash.
        activateApp();
        welcomeBridge.shown(how);
    };
    QObject::connect(&hyperwaveApp, &App::welcomeRequested, &app,
                     [&] { showWelcome(QStringLiteral("menu")); });
    // Open at Login flipped in the menu: the window's switch follows. (The
    // other way needs nothing: the menu reads the system each time it opens.)
    QObject::connect(&hyperwaveApp, &App::loginItemToggled, &welcomeBridge,
                     [&] { welcomeBridge.refreshOpenAtLogin(); });
    // The window goes before the bridge its bindings read: left alone, it
    // outlives the bridge and every binding fails on the way out.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { delete welcomeWindow; });

    // A way to see the welcome window without photographing the screen, like
    // HYPERWAVE_SPLASH_SHOT: open it straight away, grab it from the scene
    // graph after HYPERWAVE_WELCOME_SHOT_MS (default 4000, time for the store
    // and its loops to arrive), write it out and quit.
    if (qEnvironmentVariableIsSet("HYPERWAVE_WELCOME_SHOT")) {
        const QString path = qEnvironmentVariable("HYPERWAVE_WELCOME_SHOT");
        const int at = qEnvironmentVariableIntValue("HYPERWAVE_WELCOME_SHOT_MS");
        showWelcome(QStringLiteral("menu"));
        QTimer::singleShot(at > 0 ? at : 4000, &app, [&welcomeWindow, path] {
            if (!welcomeWindow)
                return;
            const QImage shot = welcomeWindow->grabWindow();
            if (shot.save(path))
                qInfo("hyperwave: wrote %s (%dx%d px)", qPrintable(path), shot.width(),
                      shot.height());
            else
                qWarning("hyperwave: could not write %s", qPrintable(path));
            qApp->quit();
        });
        return QApplication::exec();
    }

    if (options.shotPath.isEmpty() && !options.verifyAudio) {
        showSplash(4000);
        // After the splash leaves. The first showing is told apart from the
        // rest only for the count; it is recorded when shown, so quitting while
        // it is up does not make the next one a first again.
        if (hyperwaveApp.settings().welcomeAtStartup() && splashWindow)
            QObject::connect(splashWindow, &QWindow::visibleChanged, &app, [&](bool visible) {
                if (visible || !hyperwaveApp.settings().welcomeAtStartup())
                    return;
                const bool first = !hyperwaveApp.settings().welcomeShown();
                hyperwaveApp.settings().setWelcomeShown();
                showWelcome(first ? QStringLiteral("first_run") : QStringLiteral("startup"));
            });
    }
    QObject::connect(&hyperwaveApp, &App::splashRequested, &app, [&] { showSplash(0); });

    return QApplication::exec();
}
