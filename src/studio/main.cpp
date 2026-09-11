#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTimer>

#include "studio/StudioWindow.h"

using namespace hyperwave::studio;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HyperwaveStudio"));
    app.setApplicationDisplayName(QStringLiteral("Hyperwave Studio"));
    app.setOrganizationName(QStringLiteral("Hypernuclear"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#191a20"));
    palette.setColor(QPalette::WindowText, QColor("#ededf2"));
    palette.setColor(QPalette::Base, QColor("#111216"));
    palette.setColor(QPalette::AlternateBase, QColor("#24262d"));
    palette.setColor(QPalette::Text, QColor("#ededf2"));
    palette.setColor(QPalette::Button, QColor("#292b34"));
    palette.setColor(QPalette::ButtonText, QColor("#ededf2"));
    palette.setColor(QPalette::Highlight, QColor("#8271ef"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#777986"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#777986"));
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(
        "QWidget { font-size: 12px; }"
        "QPushButton { padding: 8px 12px; border: 1px solid #454752; border-radius: 6px; background: #292b34; }"
        "QPushButton:hover { background: #363844; }"
        "QPushButton:pressed { background: #202129; }"
        "QPushButton:focus { border-color: #aaa0ff; }"
        "QPushButton:disabled { color: #777986; border-color: #33353e; }"
        "QPushButton#primary { background: #8070e8; color: white; border-color: #9686fa; font-weight: 600; }"
        "QPushButton#primary:hover { background: #9180f5; }"
        "QPushButton#primary:pressed { background: #6e5ed6; }"
        "QPushButton#primary:disabled { background: #34303e; color: #85808f; border-color: #494452; }"
        "QGroupBox { border: 1px solid #3a3c46; border-radius: 8px; margin-top: 10px; padding: 18px 10px 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #c3c4d1; }"
        "QComboBox, QSpinBox, QDoubleSpinBox { min-height: 26px; }"
        "QTabWidget::pane { border: 1px solid #353741; border-radius: 8px; }"
        "QTabBar::tab { padding: 10px 24px; }"
        "QProgressBar { background: #30313b; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background: #8d7bef; border-radius: 2px; }"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Hyperwave Studio — transparent animation capture at 60 fps."));
    parser.addHelpOption();
    parser.addOptions({
        {"export", "Export without opening the studio window, into a new folder here.", "directory"},
        {"size", "Output dimensions in pixels.", "WIDTHxHEIGHT", "1456x352"},
        {"frames", "Number of frames to export at 60 fps.", "count", "600"},
        {"viz", "bars, mirror, lines, ring, strands, or ridge.", "id", "bars"},
        {"gradient", "signature, ember, lagoon, rainbow, or mono.", "id", "signature"},
        {"layout", "menu-bar, notch-collapsed, or notch-expanded.", "id", "menu-bar"},
        {"panel", "Include the black notch background in exported frames."},
        {"compact", "Use the compact menu-bar layout."},
        {"audio", "Audio file to drive the export; otherwise use the built-in demo.", "path"},
        {"system-audio", "Use live system output; for export, record the duration first."},
        {"start", "Start time in seconds.", "seconds", "0"},
        {"screenshot", "Save a screenshot of the studio window and exit.", "path"}
    });
    parser.process(app);
    if (parser.isSet("system-audio") && (parser.isSet("audio") || parser.isSet("start"))) {
        qCritical("System audio cannot be combined with --audio or --start.");
        return 1;
    }

    if (parser.isSet("export")) {
        CaptureSettings settings;
        settings.visualiser = parser.value("viz");
        settings.gradient = parser.value("gradient");
        settings.style.bloom = defaultBloom(settings.visualiser);
        const auto size = QRegularExpression(QStringLiteral("^(\\d+)x(\\d+)$")).match(parser.value("size"));
        bool framesOk = false, startOk = false;
        settings.frames = parser.value("frames").toInt(&framesOk);
        settings.startSeconds = parser.value("start").toDouble(&startOk);
        if (!size.hasMatch() || !framesOk || !startOk) {
            qCritical("Invalid size, frame count, or start time.");
            return 1;
        }
        settings.size = {size.captured(1).toInt(), size.captured(2).toInt()};
        const QString layout = parser.value("layout");
        if (layout == QLatin1String("notch-collapsed")) settings.layout = Layout::NotchCollapsed;
        else if (layout == QLatin1String("notch-expanded")) settings.layout = Layout::NotchExpanded;
        else if (layout != QLatin1String("menu-bar")) { qCritical("Unknown layout."); return 1; }
        settings.includePanel = parser.isSet("panel");
        settings.compact = parser.isSet("compact");
        ExportJob job;
        AudioLoader loader;
        SystemAudioCapture live;
        QObject::connect(&job, &ExportJob::finished, &app,
                         [&](const QString &directory, bool cancelled, const QString &error) {
            if (!error.isEmpty()) qCritical().noquote() << error;
            else qInfo().noquote() << "Saved" << job.writtenFrames() << "frames to" << directory;
            app.exit(error.isEmpty() && !cancelled ? 0 : 1);
        });
        QObject::connect(&loader, &AudioLoader::loaded, &app,
                         [&](std::shared_ptr<const AudioClip> clip) {
            job.start(settings, std::move(clip), parser.value("export"));
        });
        QObject::connect(&loader, &AudioLoader::failed, &app, [&](const QString &error) {
            qCritical().noquote() << error;
            app.exit(1);
        });
        QObject::connect(&live, &SystemAudioCapture::recorded, &app,
                         [&](std::shared_ptr<const AudioClip> clip) {
            live.stop();
            settings.startSeconds = double(kWarmupFrames) / kFps;
            job.start(settings, std::move(clip), parser.value("export"));
        });
        QObject::connect(&live, &SystemAudioCapture::failed, &app, [&](const QString &error) {
            qCritical().noquote() << error;
            app.exit(1);
        });
        QTimer::singleShot(0, &app, [&] {
            if (parser.isSet("system-audio")) {
                const auto error = validate(settings);
                if (!error.isEmpty()) { qCritical().noquote() << error; app.exit(1); return; }
                if (!live.start()) { app.exit(1); return; }
                QTimer::singleShot(100, &app, [&] {
                    if (!live.record(settings.frames)) {
                        qCritical("System audio was not ready to record.");
                        app.exit(1);
                    }
                });
            } else if (parser.isSet("audio")) loader.load(parser.value("audio"));
            else job.start(settings, {}, parser.value("export"));
        });
        return app.exec();
    }

    StudioWindow window;
    window.show();
    if (parser.isSet("system-audio")) window.useSystemAudio();
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(3500, &window, [&] {
            app.exit(window.grab().save(parser.value("screenshot")) ? 0 : 1);
        });
    }
    return app.exec();
}
