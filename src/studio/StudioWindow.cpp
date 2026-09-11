#include "studio/StudioWindow.h"

#include <algorithm>
#include <cmath>
#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMediaPlayer>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include "app/VisualiserControls.h"
#include "render/Palette.h"
#include "render/StrandRenderer.h"

#ifdef Q_OS_MACOS
#include "platform/AudioCapturePermission.h"
#endif

namespace hyperwave::studio {

class PreviewCanvas : public QWidget
{
public:
    explicit PreviewCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(320, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(QStringLiteral("Visualizer preview"));
    }
    void setFrame(const QImage &frame) { m_frame = frame; update(); }
    void setBackground(int background) { m_background = background; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#111216"));
        if (m_frame.isNull()) {
            p.setPen(QColor("#989aa6"));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Preparing preview…"));
            return;
        }
        const QSize fitted = m_frame.size().scaled(size() - QSize(64, 64), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
        p.fillRect(area, m_background == 2 ? QColor("#f2f2f4") : QColor("#060608"));
        if (m_background == 0) {
            p.save();
            p.setClipRect(area);
            constexpr int cell = 12;
            for (int y = 0; y < area.height(); y += cell)
                for (int x = 0; x < area.width(); x += cell)
                    p.fillRect(area.x() + x, area.y() + y, cell, cell,
                               ((x / cell + y / cell) % 2) ? QColor("#292b32") : QColor("#202229"));
            p.restore();
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(area, m_frame);
        p.setPen(QColor("#3b3d46"));
        p.drawRect(area.adjusted(-1, -1, 0, 0));
    }
private:
    QImage m_frame;
    int m_background = 0;
};

StudioWindow::StudioWindow()
{
    setWindowTitle(QStringLiteral("Hyperwave Studio"));
    resize(1240, 820);
    for (const QString &id : visualisers()) {
        RenderStyle style;
        style.bloom = defaultBloom(id);
        m_styles.insert(id, style);
    }
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(24, 20, 24, 20);
    rootLayout->setSpacing(16);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("hyperwave <span style='color:#9395a4'>studio</span>"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600;"));
    heading->addWidget(title);
    heading->addStretch();
    auto *badge = new QLabel(QStringLiteral("PNG + ALPHA   /   60 FPS"));
    badge->setStyleSheet(QStringLiteral("color: #aeb0bd; font-size: 11px;"));
    heading->addWidget(badge);
    rootLayout->addLayout(heading);

    auto *splitter = new QSplitter;
    auto *stage = new QWidget;
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 16, 0);
    m_canvas = new PreviewCanvas;
    stageLayout->addWidget(m_canvas, 1);
    auto *previewOptions = new QHBoxLayout;
    previewOptions->addWidget(new QLabel(QStringLiteral("Preview background")));
    auto *background = new QComboBox;
    background->addItems({QStringLiteral("Transparency"), QStringLiteral("Black"), QStringLiteral("Light")});
    background->setToolTip(QStringLiteral("Only changes the preview. Exported backgrounds stay transparent."));
    connect(background, &QComboBox::currentIndexChanged, m_canvas, &PreviewCanvas::setBackground);
    previewOptions->addWidget(background);
    previewOptions->addStretch();
    stageLayout->addLayout(previewOptions);
    m_transport = new QWidget;
    auto *transport = new QHBoxLayout(m_transport);
    transport->setContentsMargins(0, 12, 0, 0);
    auto *previous = new QPushButton(QStringLiteral("←"));
    previous->setAccessibleName(QStringLiteral("Previous visualizer"));
    auto *next = new QPushButton(QStringLiteral("→"));
    next->setAccessibleName(QStringLiteral("Next visualizer"));
    previous->setFixedWidth(44);
    next->setFixedWidth(44);
    m_lookLabel = new QLabel;
    m_lookLabel->setAlignment(Qt::AlignCenter);
    transport->addWidget(previous);
    transport->addWidget(m_lookLabel, 1);
    transport->addWidget(next);
    m_play = new QPushButton(QStringLiteral("Pause"));
    auto *restart = new QPushButton(QStringLiteral("Restart"));
    transport->addWidget(m_play);
    transport->addWidget(restart);
    stageLayout->addWidget(m_transport);
    const auto cycle = [this](int delta) {
        const QStringList ids = visualisers();
        int index = (ids.indexOf(m_visualiser) + delta + ids.size()) % ids.size();
        if (ids[index] == QLatin1String("strands") && !StrandRenderer::isAvailable())
            index = (index + delta + ids.size()) % ids.size();
        setVisualiser(ids[index]);
    };
    connect(previous, &QPushButton::clicked, this, [cycle] { cycle(-1); });
    connect(next, &QPushButton::clicked, this, [cycle] { cycle(1); });
    connect(restart, &QPushButton::clicked, this, &StudioWindow::restartPreview);
    connect(m_play, &QPushButton::clicked, this, [this] {
        m_playing = !m_playing;
        m_play->setText(m_playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        m_clockStartFrame = m_preview ? m_preview->nextFrame() : 0;
        m_clock.restart();
        if (m_playing && m_warmed && activeClip()) {
            m_player->setPosition(qRound64((m_start->value() + double(m_clockStartFrame) / kFps) * 1000));
            if (m_listen->isChecked()) m_player->play();
        } else m_player->pause();
    });
    splitter->addWidget(stage);

    auto *tabs = new QTabWidget;
    m_editor = tabs;
    tabs->setMinimumWidth(430);
    auto *capture = new QWidget;
    auto *captureLayout = new QVBoxLayout(capture);
    captureLayout->setContentsMargins(16, 18, 16, 18);
    captureLayout->setSpacing(16);
    auto *surface = new QGroupBox(QStringLiteral("Canvas"));
    auto *form = new QFormLayout(surface);
    form->setSpacing(12);
    m_layout = new QComboBox;
    m_layout->addItems({QStringLiteral("Menu bar"), QStringLiteral("Notch · collapsed"), QStringLiteral("Notch · expanded")});
    form->addRow(QStringLiteral("Layout"), m_layout);
    m_compact = new QCheckBox(QStringLiteral("Compact menu-bar strip"));
    form->addRow(m_compact);
    m_panel = new QCheckBox(QStringLiteral("Include black notch background"));
    m_panel->setEnabled(false);
    form->addRow(m_panel);
    auto *dimensions = new QWidget;
    auto *dimensionsLayout = new QHBoxLayout(dimensions);
    dimensionsLayout->setContentsMargins(0, 0, 0, 0);
    m_width = new QSpinBox;
    m_height = new QSpinBox;
    for (auto *spin : {m_width, m_height}) {
        spin->setRange(16, 8192);
        spin->setKeyboardTracking(false);
        spin->setSuffix(QStringLiteral(" px"));
        dimensionsLayout->addWidget(spin);
    }
    m_width->setAccessibleName(QStringLiteral("Output width"));
    m_height->setAccessibleName(QStringLiteral("Output height"));
    m_width->setValue(1456);
    m_height->setValue(352);
    form->addRow(QStringLiteral("Size"), dimensions);
    m_lockAspect = new QCheckBox(QStringLiteral("Match layout aspect ratio"));
    m_lockAspect->setChecked(true);
    m_lockAspect->setToolTip(QStringLiteral("Unlock for a custom canvas. The visualizer stays proportional and centered."));
    form->addRow(m_lockAspect);
    m_gradient = new QComboBox;
    for (const auto &preset : Palette::presets()) m_gradient->addItem(preset.name, preset.id);
    form->addRow(QStringLiteral("Palette"), m_gradient);
    captureLayout->addWidget(surface);

    auto *audio = new QGroupBox(QStringLiteral("Audio && timing"));
    auto *audioLayout = new QVBoxLayout(audio);
    m_audioSource = new QComboBox;
    m_audioSource->addItems({QStringLiteral("Demo audio"), QStringLiteral("Audio file"),
                             QStringLiteral("System audio")});
    m_audioSource->setAccessibleName(QStringLiteral("Audio source"));
    audioLayout->addWidget(m_audioSource);
    m_audioLabel = new QLabel(QStringLiteral("Demo audio · repeatable musical motion"));
    m_audioLabel->setWordWrap(true);
    audioLayout->addWidget(m_audioLabel);
    auto *audioButtons = new QHBoxLayout;
    auto *loadAudio = new QPushButton(QStringLiteral("Open audio…"));
    auto *demo = new QPushButton(QStringLiteral("Use demo"));
    audioButtons->addWidget(loadAudio);
    audioButtons->addWidget(demo);
    audioLayout->addLayout(audioButtons);
    m_systemOptions = new QWidget;
    auto *systemOptions = new QHBoxLayout(m_systemOptions);
    systemOptions->setContentsMargins(0, 0, 0, 0);
    auto *retrySystem = new QPushButton(QStringLiteral("Retry capture"));
    systemOptions->addWidget(retrySystem);
    connect(retrySystem, &QPushButton::clicked, this, [this] {
        m_systemAudio.stop();
        selectAudioSource();
    });
#ifdef Q_OS_MACOS
    auto *access = new QPushButton(QStringLiteral("System Audio Access…"));
    access->setToolTip(QStringLiteral("When debugging, grant System Audio Recording to Visual Studio Code. "
                                      "When launched normally, grant it to Hyperwave Studio. Then retry capture."));
    systemOptions->addWidget(access);
    connect(access, &QPushButton::clicked, this, [] { AudioCapturePermission::openSystemSettings(); });
#endif
    m_systemOptions->hide();
    audioLayout->addWidget(m_systemOptions);
    m_listen = new QCheckBox(QStringLiteral("Play soundtrack during preview"));
    m_listen->setChecked(true);
    m_listen->setEnabled(false);
    audioLayout->addWidget(m_listen);
    auto *timing = new QFormLayout;
    m_start = new QDoubleSpinBox;
    m_start->setRange(0, 600);
    m_start->setDecimals(3);
    m_start->setKeyboardTracking(false);
    m_start->setSuffix(QStringLiteral(" s"));
    m_duration = new QDoubleSpinBox;
    m_duration->setRange(1.0 / kFps, 300);
    m_duration->setDecimals(3);
    m_duration->setValue(10);
    m_duration->setKeyboardTracking(false);
    m_duration->setSuffix(QStringLiteral(" s"));
    timing->addRow(QStringLiteral("Start"), m_start);
    timing->addRow(QStringLiteral("Duration"), m_duration);
    audioLayout->addLayout(timing);
    captureLayout->addWidget(audio);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#a9abb9;"));
    captureLayout->addWidget(m_info);
    captureLayout->addStretch();
    auto *captureScroll = new QScrollArea;
    captureScroll->setWidgetResizable(true);
    captureScroll->setFrameShape(QFrame::NoFrame);
    captureScroll->setWidget(capture);
    tabs->addTab(captureScroll, QStringLiteral("Capture"));
    auto *lookScroll = new QScrollArea;
    lookScroll->setWidgetResizable(true);
    lookScroll->setFrameShape(QFrame::NoFrame);
    m_controls = new VisualiserControls({false, true});
    m_controls->setContentsMargins(12, 18, 12, 18);
    lookScroll->setWidget(m_controls);
    tabs->addTab(lookScroll, QStringLiteral("Look"));
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({730, 440});
    rootLayout->addWidget(splitter, 1);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 600);
    m_progress->setValue(0);
    m_progress->setFixedHeight(5);
    m_progress->setTextVisible(false);
    rootLayout->addWidget(m_progress);
    auto *footer = new QHBoxLayout;
    m_status = new QLabel(QStringLiteral("Ready. Exports start at the selected time."));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    footer->addWidget(m_status, 1);
    m_reveal = new QPushButton(QStringLiteral("Open export"));
    m_reveal->setVisible(false);
    m_still = new QPushButton(QStringLiteral("Save frame…"));
    m_export = new QPushButton(QStringLiteral("Export PNG sequence…"));
    m_export->setObjectName(QStringLiteral("primary"));
    footer->addWidget(m_reveal);
    footer->addWidget(m_still);
    footer->addWidget(m_export);
    rootLayout->addLayout(footer);
    setCentralWidget(root);

    m_audioOutput = new QAudioOutput(this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOutput);
    connect(m_listen, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled) m_player->pause();
        else if (m_playing && activeClip() && m_warmed) {
            m_player->setPosition(qRound64((m_start->value() + double(m_preview->nextFrame()) / kFps) * 1000));
            m_player->play();
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) {
        m_status->setText(QStringLiteral("Soundtrack playback: %1. PNG export is still available.").arg(error));
    });
    connect(loadAudio, &QPushButton::clicked, this, &StudioWindow::chooseAudio);
    connect(m_audioSource, &QComboBox::currentIndexChanged, this, &StudioWindow::selectAudioSource);
    connect(demo, &QPushButton::clicked, this, [this] {
        m_audioSource->setCurrentIndex(0);
    });
    connect(&m_loader, &AudioLoader::loaded, this, [this](std::shared_ptr<const AudioClip> clip) {
        m_clip = std::move(clip);
        m_systemAudio.stop();
        {
            QSignalBlocker block(m_audioSource);
            m_audioSource->setCurrentIndex(1);
        }
        m_player->setSource(QUrl::fromLocalFile(m_clip->path));
        m_audioLabel->setText(QStringLiteral("%1 · %2 s").arg(QFileInfo(m_clip->path).fileName())
                                 .arg(m_clip->duration(), 0, 'f', 2));
        m_audioLabel->setToolTip(m_clip->path);
        m_start->setValue(0);
        m_duration->setValue(std::min(m_duration->value(), std::floor(m_clip->duration() * kFps) / kFps));
        setBusy(false);
        m_listen->setEnabled(true);
        m_status->setText(QStringLiteral("Audio loaded. Add the same soundtrack separately in After Effects."));
        selectAudioSource();
    });
    connect(&m_loader, &AudioLoader::failed, this, [this](const QString &error) {
        if (usesSystemAudio()) m_systemAudio.start();
        setBusy(false);
        m_status->setText(QStringLiteral("Could not load audio: %1").arg(error));
        if (!activeClip() && m_audioSource->currentIndex() == 1)
            m_audioSource->setCurrentIndex(0);
        restartPreview();
    });
    connect(&m_systemAudio, &SystemAudioCapture::progress, this, [this](int frames, int total) {
        if (!m_recordingSystem) return;
        m_progress->setRange(0, total);
        m_progress->setValue(frames);
        m_status->setText(QStringLiteral("Recording system audio · %1 / %2 s")
                             .arg(double(frames) / kFps, 0, 'f', 1)
                             .arg(double(total) / kFps, 0, 'f', 1));
    });
    connect(&m_systemAudio, &SystemAudioCapture::recorded, this, [this](std::shared_ptr<const AudioClip> clip) {
        if (!m_recordingSystem) return;
        m_recordingSystem = false;
        m_preview.reset();
        m_systemAudio.stop();
        m_recordingSettings.startSeconds = double(kWarmupFrames) / kFps;
        m_export->setText(QStringLiteral("Cancel export"));
        m_progress->setRange(0, 0);
        m_status->setText(QStringLiteral("Audio recorded. Rendering PNG sequence…"));
        m_exporter.start(m_recordingSettings, std::move(clip), m_recordingParent);
    });
    connect(&m_systemAudio, &SystemAudioCapture::failed, this, [this](const QString &error) {
        if (m_recordingSystem) {
            m_recordingSystem = false;
            setBusy(false);
            m_progress->setRange(0, 1);
            m_progress->setValue(0);
        }
        m_status->setText(error);
    });
    connect(m_layout, &QComboBox::currentIndexChanged, this, [this] {
        m_panel->setEnabled(m_layout->currentIndex() != 0);
        m_compact->setEnabled(m_layout->currentIndex() == 0);
        fitDimensions();
        restartPreview();
    });
    connect(m_compact, &QCheckBox::toggled, this, [this] { fitDimensions(); restartPreview(); });
    connect(m_panel, &QCheckBox::toggled, this, &StudioWindow::restartPreview);
    connect(m_gradient, &QComboBox::currentIndexChanged, this, &StudioWindow::restartPreview);
    connect(m_lockAspect, &QCheckBox::toggled, this, [this] { fitDimensions(); restartPreview(); });
    connect(m_width, &QSpinBox::valueChanged, this, [this] {
        if (m_adjusting) return;
        fitDimensions();
        restartPreview();
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this] {
        if (m_adjusting) return;
        if (m_lockAspect->isChecked()) {
            QScopedValueRollback guard(m_adjusting, true);
            const QSizeF native = nativeSize(settings());
            m_width->setValue(qRound(m_height->value() * native.width() / native.height()));
        }
        restartPreview();
    });
    connect(m_start, &QDoubleSpinBox::valueChanged, this, &StudioWindow::restartPreview);
    connect(m_duration, &QDoubleSpinBox::valueChanged, this, &StudioWindow::restartPreview);
    connect(m_controls, &VisualiserControls::visualiserRequested, this, &StudioWindow::setVisualiser);
    connect(m_controls, &VisualiserControls::resetRequested, this, [this] {
        m_styles[m_visualiser] = RenderStyle{};
        m_styles[m_visualiser].bloom = defaultBloom(m_visualiser);
        seedControls();
        restartPreview();
    });
    const auto bind = [this](auto signal, auto member) {
        connect(m_controls, signal, this, [this, member](const auto &value) {
            m_styles[m_visualiser].*member = value;
            restartPreview();
        });
    };
    bind(&VisualiserControls::strandsRequested, &RenderStyle::strands);
    bind(&VisualiserControls::ridgeRequested, &RenderStyle::ridge);
    bind(&VisualiserControls::linesRequested, &RenderStyle::lines);
    bind(&VisualiserControls::mirrorRequested, &RenderStyle::mirror);
    bind(&VisualiserControls::ringRequested, &RenderStyle::ring);
    bind(&VisualiserControls::bloomRequested, &RenderStyle::bloom);
    connect(m_still, &QPushButton::clicked, this, &StudioWindow::saveStill);
    connect(m_export, &QPushButton::clicked, this, [this] {
        if (m_recordingSystem) {
            m_recordingSystem = false;
            m_systemAudio.cancelRecording();
            setBusy(false);
            m_status->setText(QStringLiteral("Recording cancelled. No take was exported."));
        } else if (m_exporter.running()) m_exporter.cancel();
        else exportSequence();
    });
    connect(m_reveal, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastDirectory));
    });
    connect(&m_exporter, &ExportJob::progress, this, [this](int written, int total, const QImage &frame) {
        m_progress->setRange(0, total);
        m_progress->setValue(written);
        m_canvas->setFrame(frame);
        m_status->setText(QStringLiteral("Rendering %1 / %2 frames · %3 s at 60 fps")
                             .arg(written).arg(total).arg(double(written) / kFps, 0, 'f', 2));
    });
    connect(&m_exporter, &ExportJob::finished, this,
            [this](const QString &directory, bool cancelled, const QString &error) {
        setBusy(false);
        if (!directory.isEmpty()) {
            m_lastDirectory = directory;
            m_reveal->setVisible(true);
        }
        m_progress->setRange(0, std::max(1, settings().frames));
        m_progress->setValue(m_exporter.writtenFrames());
        m_status->setText(!error.isEmpty() ? QStringLiteral("Export failed: %1").arg(error)
            : cancelled ? QStringLiteral("Cancelled · %1 completed frames kept.").arg(m_exporter.writtenFrames())
            : QStringLiteral("Saved %1 frames. Import as a PNG sequence at 60 fps, straight alpha.").arg(m_exporter.writtenFrames()));
        if (usesSystemAudio()) m_systemAudio.start();
        restartPreview();
    });
    connect(&m_previewTimer, &QTimer::timeout, this, &StudioWindow::tickPreview);
    m_previewTimer.setTimerType(Qt::PreciseTimer);
    m_previewTimer.start(16);
    const QByteArray geometry = QSettings().value(QStringLiteral("studio/windowGeometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    seedControls();
    restartPreview();
}

StudioWindow::~StudioWindow()
{
    // Disconnect before cancelling: cancellation normally restarts the preview.
    disconnect(&m_exporter, nullptr, this, nullptr);
    m_exporter.cancel();
    disconnect(&m_systemAudio, nullptr, this, nullptr);
    m_systemAudio.stop();
    QSettings().setValue(QStringLiteral("studio/windowGeometry"), saveGeometry());
}

CaptureSettings StudioWindow::settings() const
{
    CaptureSettings s;
    s.visualiser = m_visualiser;
    s.style = m_styles.value(m_visualiser);
    s.gradient = m_gradient->currentData().toString();
    s.layout = static_cast<Layout>(m_layout->currentIndex());
    s.size = {m_width->value(), m_height->value()};
    s.frames = std::max(1, qRound(m_duration->value() * kFps));
    s.startSeconds = usesSystemAudio() ? 0 : m_start->value();
    s.includePanel = m_panel->isChecked();
    s.compact = m_compact->isChecked();
    return s;
}

void StudioWindow::seedControls()
{
    const auto style = m_styles.value(m_visualiser);
    m_controls->setVisualiser(m_visualiser);
    m_controls->setStrands(style.strands);
    m_controls->setRidge(style.ridge);
    m_controls->setLines(style.lines);
    m_controls->setMirror(style.mirror);
    m_controls->setRing(style.ring);
    m_controls->setBloom(style.bloom);
    m_lookLabel->setText(m_visualiser == QLatin1String("ridge") ? QStringLiteral("Terrain")
                       : m_visualiser.left(1).toUpper() + m_visualiser.mid(1));
}

void StudioWindow::setVisualiser(const QString &id)
{
    m_visualiser = id;
    seedControls();
    fitDimensions();
    restartPreview();
}

void StudioWindow::fitDimensions()
{
    if (!m_lockAspect->isChecked()) return;
    QScopedValueRollback guard(m_adjusting, true);
    const QSizeF native = nativeSize(settings());
    m_height->setValue(qRound(m_width->value() * native.height() / native.width()));
}

void StudioWindow::updateInfo()
{
    const auto s = settings();
    const QString error = validate(s, activeClip().get());
    m_info->setText(error.isEmpty()
        ? QStringLiteral("%1 × %2 px · %3 frames · %4 s\nStraight alpha · sRGB · 60 fps\nPreview backgrounds are never exported.")
              .arg(s.size.width()).arg(s.size.height()).arg(s.frames).arg(double(s.frames) / kFps, 0, 'f', 3)
        : error);
    m_export->setEnabled(m_exporter.running() || m_recordingSystem
        || (!m_loader.loading() && error.isEmpty() && (!usesSystemAudio() || m_systemAudio.running())));
    if (!m_exporter.running() && !m_recordingSystem && !m_loader.loading())
        m_export->setText(usesSystemAudio() ? QStringLiteral("Record && export…") : QStringLiteral("Export PNG sequence…"));
}

void StudioWindow::restartPreview()
{
    if (m_adjusting || m_exporter.running() || m_recordingSystem || m_loader.loading()) return;
    m_player->pause();
    m_preview.reset();
    m_lastFrame = {};
    m_still->setEnabled(false);
    m_warmed = false;
    m_clockStartFrame = 0;
    updateInfo();
    try {
        auto s = settings();
        // Preview the exact output pixels, including export-size bloom.
        m_preview = std::make_unique<RenderSession>(s, activeClip());
        m_clock.restart();
    } catch (const std::exception &e) {
        m_canvas->setFrame({});
        m_status->setText(QString::fromUtf8(e.what()));
        m_export->setEnabled(false);
    }
}

void StudioWindow::tickPreview()
{
    if (!m_preview || m_exporter.running() || m_loader.loading()) return;
    try {
        if (usesSystemAudio()) {
            if (!m_playing || !m_systemAudio.running()) return;
            const auto state = m_systemAudio.frame();
            m_lastFrame = m_preview->renderLive(state, double(m_clock.elapsed()) / 1000);
            m_canvas->setFrame(m_lastFrame);
            m_still->setEnabled(!m_recordingSystem);
            m_audioLabel->setText(state.rmsDb > -100
                ? QStringLiteral("System audio · %1 dB").arg(state.rmsDb, 0, 'f', 1)
                : QStringLiteral("System audio · quiet. Check access if music is playing."));
            return;
        }
        if (!m_warmed) {
            for (int i = 0; i < 4 && m_preview->nextFrame() <= 0; ++i)
                m_lastFrame = m_preview->step();
            if (m_preview->nextFrame() > 0) {
                m_warmed = true;
                m_clockStartFrame = 0;
                m_clock.restart();
                m_canvas->setFrame(m_lastFrame);
                m_still->setEnabled(true);
                if (activeClip() && m_playing && m_listen->isChecked()) {
                    m_player->setPosition(qRound64(m_start->value() * 1000));
                    m_player->play();
                }
            }
            return;
        }
        if (!m_playing) return;
        const int due = m_clockStartFrame + int(m_clock.elapsed() * kFps / 1000);
        for (int i = 0; i < 4 && m_preview->nextFrame() <= due; ++i) {
            if (m_preview->nextFrame() >= m_preview->settings().frames) {
                restartPreview();
                return;
            }
            m_lastFrame = m_preview->step();
        }
        m_canvas->setFrame(m_lastFrame);
    } catch (const std::exception &e) {
        m_preview.reset();
        m_player->pause();
        m_status->setText(QString::fromUtf8(e.what()));
    }
}

void StudioWindow::chooseAudio()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open soundtrack"), {},
        QStringLiteral("Audio (*.wav *.aif *.aiff *.mp3 *.m4a *.flac *.ogg);;All files (*)"));
    if (path.isEmpty()) {
        if (m_audioSource->currentIndex() == 1 && !m_clip) m_audioSource->setCurrentIndex(0);
        return;
    }
    m_systemAudio.stop();
    m_player->pause();
    setBusy(true);
    m_export->setText(QStringLiteral("Loading audio…"));
    m_export->setEnabled(false);
    m_status->setText(QStringLiteral("Decoding %1…").arg(QFileInfo(path).fileName()));
    m_loader.load(path);
}

void StudioWindow::setBusy(bool busy)
{
    m_editor->setEnabled(!busy);
    m_transport->setEnabled(!busy);
    m_still->setEnabled(!busy && !m_lastFrame.isNull());
    m_export->setText(busy ? QStringLiteral("Cancel export") : QStringLiteral("Export PNG sequence…"));
    if (!busy) updateInfo();
}

void StudioWindow::exportSequence()
{
    const QString parent = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose export location"),
        QSettings().value(QStringLiteral("studio/exportParent"),
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString());
    if (parent.isEmpty()) return;
    QSettings().setValue(QStringLiteral("studio/exportParent"), parent);
    m_player->pause();
    if (usesSystemAudio()) {
        m_recordingSettings = settings();
        m_recordingParent = parent;
        m_recordingSystem = m_systemAudio.record(m_recordingSettings.frames);
        if (!m_recordingSystem) {
            m_status->setText(QStringLiteral("System audio is still starting. Wait a moment and retry."));
            return;
        }
        setBusy(true);
        m_export->setText(QStringLiteral("Cancel recording"));
        m_progress->setRange(0, m_recordingSettings.frames);
        m_progress->setValue(0);
        m_status->setText(QStringLiteral("Recording system audio…"));
        return;
    }
    m_preview.reset();
    setBusy(true);
    m_progress->setRange(0, 0);
    m_status->setText(QStringLiteral("Preparing animation…"));
    m_exporter.start(settings(), activeClip(), parent);
}

bool StudioWindow::usesSystemAudio() const
{
    return m_audioSource && m_audioSource->currentIndex() == 2;
}

void StudioWindow::useSystemAudio()
{
    m_audioSource->setCurrentIndex(2);
}

std::shared_ptr<const AudioClip> StudioWindow::activeClip() const
{
    return m_audioSource && m_audioSource->currentIndex() == 1 ? m_clip : nullptr;
}

void StudioWindow::selectAudioSource()
{
    m_player->pause();
    m_systemOptions->setVisible(usesSystemAudio());
    m_start->setEnabled(!usesSystemAudio());
    m_listen->setEnabled(bool(activeClip()));
    if (usesSystemAudio()) {
        m_audioLabel->setText(QStringLiteral("Starting system audio…"));
        m_audioLabel->setToolTip(QStringLiteral("Captures system output only; the microphone is excluded. "
                                              "Record & export captures the duration from now, then renders at 60 fps."));
        m_systemAudio.start();
        m_status->setText(m_systemAudio.running()
            ? QStringLiteral("Listening live. Record & export captures audio first, then renders PNGs.")
            : QStringLiteral("System audio could not start. Check audio access, then retry capture."));
    } else {
        m_systemAudio.stop();
        if (m_audioSource->currentIndex() == 1 && !m_clip) {
            chooseAudio();
            return;
        }
        m_audioLabel->setText(activeClip()
            ? QStringLiteral("%1 · %2 s").arg(QFileInfo(m_clip->path).fileName()).arg(m_clip->duration(), 0, 'f', 2)
            : QStringLiteral("Demo audio · repeatable musical motion"));
        m_audioLabel->setToolTip(activeClip() ? m_clip->path : QString());
    }
    restartPreview();
}

void StudioWindow::saveStill()
{
    if (m_lastFrame.isNull()) return;
    const QImage frame = m_lastFrame;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save transparent frame"),
        QStringLiteral("hyperwave-%1.png").arg(m_visualiser), QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) return;
    QString error;
    m_status->setText(savePng(frame, path, &error)
        ? QStringLiteral("Saved %1 × %2 px with transparency.").arg(frame.width()).arg(frame.height())
        : QStringLiteral("Could not save frame: %1").arg(error));
}

} // namespace hyperwave::studio
