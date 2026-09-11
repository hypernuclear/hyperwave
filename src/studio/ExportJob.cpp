#include "studio/ExportJob.h"

#include <stdexcept>
#include <algorithm>
#include <QDataStream>
#include <QtEndian>

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace hyperwave::studio {

ExportJob::ExportJob(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &ExportJob::tick);
}

bool ExportJob::start(const CaptureSettings &settings, std::shared_ptr<const AudioClip> clip,
                      const QString &parentDirectory)
{
    if (running()) return false;
    m_directory.clear();
    m_written = 0;
    try {
        m_session = std::make_unique<RenderSession>(settings, clip);
        m_clip = std::move(clip);
        const QString name = QStringLiteral("hyperwave-%1-%2-%3")
            .arg(settings.visualiser, QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                 QUuid::createUuid().toString(QUuid::Id128).left(8));
        QDir parent(parentDirectory);
        if (!parent.exists() || !parent.mkdir(name))
            throw std::runtime_error("Could not create an export folder in that location.");
        m_directory = parent.absoluteFilePath(name);
        QString error;
        if (!writeManifest(false, &error)) throw std::runtime_error(error.toStdString());
        if (m_clip && m_clip->systemAudio && !writeCapturedAudio(&error))
            throw std::runtime_error(error.toStdString());
        m_timer.start();
        return true;
    } catch (const std::exception &e) {
        finish(false, QString::fromUtf8(e.what()));
        return false;
    }
}

void ExportJob::tick()
{
    if (!m_session) return;
    try {
        const int frame = m_session->nextFrame();
        const QImage image = m_session->step();
        if (frame < 0) return;
        const QString name = QStringLiteral("frame_%1.png").arg(frame, 6, 10, QLatin1Char('0'));
        QString error;
        if (!savePng(image, QDir(m_directory).filePath(name), &error)) {
            finish(false, error);
            return;
        }
        ++m_written;
        Q_EMIT progress(m_written, m_session->settings().frames, image);
        // A progress handler may cancel the job synchronously.
        if (m_session && m_written == m_session->settings().frames) finish(false);
    } catch (const std::exception &e) {
        finish(false, QString::fromUtf8(e.what()));
    }
}

void ExportJob::cancel()
{
    if (running()) finish(true);
}

bool ExportJob::writeManifest(bool complete, QString *error)
{
    const CaptureSettings &s = m_session->settings();
    const QJsonObject metadata{
        {"fps", kFps}, {"width", s.size.width()}, {"height", s.size.height()},
        {"requestedFrames", s.frames}, {"writtenFrames", m_written}, {"complete", complete},
        {"visualiser", s.visualiser}, {"gradient", s.gradient}, {"layout", layoutId(s.layout)},
        {"includePanel", s.includePanel}, {"compact", s.compact},
        {"startSeconds", s.startSeconds}, {"durationSeconds", double(m_written) / kFps},
        {"source", m_clip ? m_clip->path : QStringLiteral("demo")},
        {"audioFile", m_clip && m_clip->systemAudio ? QStringLiteral("audio.wav") : QString()},
        {"audioFileStartSeconds", m_clip && m_clip->systemAudio ? 0.0 : s.startSeconds},
        {"alpha", "straight"}, {"colorSpace", "sRGB"}, {"sequence", "frame_%06d.png"},
        {"afterEffects", m_clip && m_clip->systemAudio
            ? "Import PNG sequence at 60 fps with Straight (Unmatted) alpha. Place audio.wav at the same start; no trimming needed."
            : "Import as PNG sequence; Interpret Footage: 60 fps, Straight (Unmatted). Add the source audio separately, trimmed to startSeconds."}};
    QSaveFile file(QDir(m_directory).filePath("capture.json"));
    const QByteArray json = QJsonDocument(metadata).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool ExportJob::writeCapturedAudio(QString *error)
{
    const auto &s = m_session->settings();
    const qint64 start = qRound64(s.startSeconds * m_clip->sampleRate);
    const qint64 count = qRound64(double(s.frames) * m_clip->sampleRate / kFps);
    QSaveFile file(QDir(m_directory).filePath("audio.wav"));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    // The mono analysis mix, trimmed to the first exported frame. PCM16 WAV
    // imports broadly, including in AE, and does not require another encoder.
    QDataStream header(&file);
    header.setByteOrder(QDataStream::LittleEndian);
    header.writeRawData("RIFF", 4); header << quint32(36 + count * 2);
    header.writeRawData("WAVEfmt ", 8); header << quint32(16) << quint16(1) << quint16(1);
    header << quint32(m_clip->sampleRate) << quint32(m_clip->sampleRate * 2) << quint16(2) << quint16(16);
    header.writeRawData("data", 4); header << quint32(count * 2);
    QByteArray block(8192, Qt::Uninitialized);
    for (qint64 i = 0; i < count;) {
        const int n = int(std::min<qint64>(4096, count - i));
        for (int j = 0; j < n; ++j) {
            const float value = std::clamp(m_clip->samples[start + i + j], -1.0f, 1.0f);
            qToLittleEndian<qint16>(qint16(qRound(value * 32767)), block.data() + j * 2);
        }
        if (file.write(block.constData(), n * 2) != n * 2) {
            *error = file.errorString();
            return false;
        }
        i += n;
    }
    if (header.status() != QDataStream::Ok || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

void ExportJob::finish(bool cancelled, QString error)
{
    m_timer.stop();
    if (m_session && !m_directory.isEmpty()) {
        QString manifestError;
        if (!writeManifest(!cancelled && error.isEmpty(), &manifestError)) {
            if (!error.isEmpty()) error += QLatin1Char('\n');
            error += manifestError;
        }
    }
    m_session.reset();
    m_clip.reset();
    Q_EMIT finished(m_directory, cancelled, error);
}

} // namespace hyperwave::studio
