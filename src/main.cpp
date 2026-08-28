#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include "app/App.h"
#include "app/Shell.h"

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

    if (options.shotPath.isEmpty() && !options.verifyAudio)
        showSplash(4000);
    QObject::connect(&hyperwaveApp, &App::splashRequested, &app, [&] { showSplash(0); });

    return QApplication::exec();
}
