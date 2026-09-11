#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QHash>
#include "studio/AudioLoader.h"
#include "studio/ExportJob.h"
#include "studio/SystemAudioCapture.h"

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace hyperwave { class VisualiserControls; }

namespace hyperwave::studio {
class PreviewCanvas;

class StudioWindow : public QMainWindow
{
    Q_OBJECT
public:
    StudioWindow();
    ~StudioWindow() override;
    void useSystemAudio();
private:
    CaptureSettings settings() const;
    void restartPreview();
    void tickPreview();
    void setVisualiser(const QString &id);
    void seedControls();
    void fitDimensions();
    void updateInfo();
    void chooseAudio();
    void exportSequence();
    void saveStill();
    void setBusy(bool busy);
    void selectAudioSource();
    bool usesSystemAudio() const;
    std::shared_ptr<const AudioClip> activeClip() const;

    PreviewCanvas *m_canvas = nullptr;
    QWidget *m_editor = nullptr;
    QWidget *m_transport = nullptr;
    VisualiserControls *m_controls = nullptr;
    QComboBox *m_layout = nullptr;
    QComboBox *m_gradient = nullptr;
    QComboBox *m_audioSource = nullptr;
    QWidget *m_systemOptions = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QDoubleSpinBox *m_duration = nullptr;
    QDoubleSpinBox *m_start = nullptr;
    QCheckBox *m_panel = nullptr;
    QCheckBox *m_compact = nullptr;
    QCheckBox *m_lockAspect = nullptr;
    QCheckBox *m_listen = nullptr;
    QLabel *m_audioLabel = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_lookLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_still = nullptr;
    QPushButton *m_play = nullptr;
    QPushButton *m_reveal = nullptr;
    QString m_visualiser = QStringLiteral("bars");
    QHash<QString, RenderStyle> m_styles;
    std::shared_ptr<const AudioClip> m_clip;
    std::unique_ptr<RenderSession> m_preview;
    QImage m_lastFrame;
    QTimer m_previewTimer;
    QElapsedTimer m_clock;
    AudioLoader m_loader;
    ExportJob m_exporter;
    SystemAudioCapture m_systemAudio;
    bool m_recordingSystem = false;
    QString m_recordingParent;
    CaptureSettings m_recordingSettings;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    bool m_playing = true;
    bool m_adjusting = false;
    bool m_warmed = false;
    int m_clockStartFrame = 0;
    QString m_lastDirectory;
};

} // namespace hyperwave::studio
