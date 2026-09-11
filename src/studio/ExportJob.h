#pragma once

#include <QObject>
#include <QTimer>
#include "studio/RenderSession.h"

namespace hyperwave::studio {

class ExportJob : public QObject
{
    Q_OBJECT
public:
    explicit ExportJob(QObject *parent = nullptr);
    bool start(const CaptureSettings &settings, std::shared_ptr<const AudioClip> clip,
               const QString &parentDirectory);
    void cancel();
    bool running() const { return m_session != nullptr; }
    QString directory() const { return m_directory; }
    int writtenFrames() const { return m_written; }

Q_SIGNALS:
    void progress(int written, int total, const QImage &frame);
    void finished(const QString &directory, bool cancelled, const QString &error);

private:
    void tick();
    void finish(bool cancelled, QString error = {});
    bool writeManifest(bool complete, QString *error);
    bool writeCapturedAudio(QString *error);
    QTimer m_timer;
    std::unique_ptr<RenderSession> m_session;
    std::shared_ptr<const AudioClip> m_clip;
    QString m_directory;
    int m_written = 0;
};

} // namespace hyperwave::studio
