#pragma once

#include <atomic>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include "audio/AudioSource.h"
#include "studio/RenderSession.h"

namespace hyperwave::studio {

// CoreAudio/WASAPI controls stay on the main thread. A dedicated consumer
// drains every sample even when the preview is expensive or a dialog is open.
class SystemAudioCapture : public QObject
{
    Q_OBJECT
public:
    explicit SystemAudioCapture(QObject *parent = nullptr);
    explicit SystemAudioCapture(std::unique_ptr<AudioSource> source, QObject *parent = nullptr);
    ~SystemAudioCapture() override;
    bool start();
    void stop();
    bool running() const { return m_running; }
    FrameState frame() const;
    bool record(int frames);
    void cancelRecording();
    bool recording() const;

Q_SIGNALS:
    void progress(int frames, int total);
    void recorded(std::shared_ptr<const AudioClip> clip);
    void failed(const QString &message);

private:
    void consume();
    void syncFormat();
    std::unique_ptr<AudioSource> m_source;
    std::unique_ptr<QThread> m_thread;
    QTimer m_health;
    std::atomic<bool> m_quit{false};
    bool m_running = false;
    mutable QMutex m_mutex;
    int m_rate = 0;
    unsigned m_epoch = 0;
    FrameState m_frame;
    QVector<float> m_history;
    qsizetype m_historyCursor = 0;
    qsizetype m_historyFilled = 0;
    std::shared_ptr<AudioClip> m_recorded;
    qsizetype m_recordCursor = 0;
    int m_recordFrames = 0;
};

} // namespace hyperwave::studio
