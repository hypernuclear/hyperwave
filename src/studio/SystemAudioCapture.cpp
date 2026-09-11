#include "studio/SystemAudioCapture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QElapsedTimer>
#include <QMutexLocker>

#ifdef Q_OS_MACOS
#include "platform/CoreAudioTapSource.h"
#endif

namespace hyperwave::studio {
namespace {
std::unique_ptr<AudioSource> captureSource()
{
#ifdef Q_OS_MACOS
    auto source = std::make_unique<CoreAudioTapSource>();
    source->setCaptureOnly(true);
    return source;
#else
    return AudioSource::create();
#endif
}
} // namespace

SystemAudioCapture::SystemAudioCapture(QObject *parent)
    : SystemAudioCapture(captureSource(), parent) {}

SystemAudioCapture::SystemAudioCapture(std::unique_ptr<AudioSource> source, QObject *parent)
    : QObject(parent), m_source(std::move(source))
{
    m_health.setInterval(100);
    connect(&m_health, &QTimer::timeout, this, &SystemAudioCapture::syncFormat);
    connect(m_source.get(), &AudioSource::formatChanged, this, &SystemAudioCapture::syncFormat);
}

SystemAudioCapture::~SystemAudioCapture() { stop(); }

bool SystemAudioCapture::start()
{
    if (m_running) return true;
    m_source->setInputEnabled(false);
    if (!m_source->start()) {
        m_source->stop();
        Q_EMIT failed(QStringLiteral("Could not start system audio: %1. Check System Audio Access, then retry.")
                          .arg(m_source->statusDetail()));
        return false;
    }
    m_running = true;
    syncFormat();
    m_source->ring()->reset();
    m_quit.store(false);
    m_thread.reset(QThread::create([this] { consume(); }));
    m_thread->setObjectName(QStringLiteral("studio.system-audio"));
    m_thread->start();
    m_health.start();
    return true;
}

void SystemAudioCapture::stop()
{
    m_health.stop();
    m_quit.store(true);
    if (m_thread) {
        m_thread->wait();
        m_thread.reset();
    }
    m_source->stop();
    m_running = false;
    QMutexLocker lock(&m_mutex);
    m_recorded.reset();
    m_history.clear();
    m_historyCursor = m_historyFilled = 0;
    m_frame = {};
}

void SystemAudioCapture::syncFormat()
{
    QMutexLocker lock(&m_mutex);
    m_rate = qRound(m_source->sampleRate());
    m_epoch = m_source->formatEpoch();
}

FrameState SystemAudioCapture::frame() const
{
    QMutexLocker lock(&m_mutex);
    return m_frame;
}

bool SystemAudioCapture::record(int frames)
{
    QMutexLocker lock(&m_mutex);
    if (!m_running || m_recorded || m_history.isEmpty() || frames < 1 || frames > kFps * 300)
        return false;
    m_recorded = std::make_shared<AudioClip>();
    m_recorded->sampleRate = m_rate;
    m_recorded->path = QStringLiteral("system-audio");
    m_recorded->systemAudio = true;
    const qsizetype preRoll = m_history.size();
    m_recorded->samples.fill(0, preRoll + qRound64(double(frames) * m_rate / kFps));
    const qsizetype begin = (m_historyCursor + preRoll - m_historyFilled) % preRoll;
    for (qsizetype i = 0; i < m_historyFilled; ++i)
        m_recorded->samples[preRoll - m_historyFilled + i] = m_history[(begin + i) % preRoll];
    m_recordCursor = preRoll;
    m_recordFrames = frames;
    return true;
}

void SystemAudioCapture::cancelRecording()
{
    QMutexLocker lock(&m_mutex);
    m_recorded.reset();
}

bool SystemAudioCapture::recording() const
{
    QMutexLocker lock(&m_mutex);
    return m_recorded != nullptr;
}

void SystemAudioCapture::consume()
{
    SpectrumAnalyzer analyzer;
    FrameState state;
    int appliedRate = 0;
    unsigned appliedEpoch = 0;
    quint64 overruns = m_source->overrunCount();
    std::array<float, 4096> block{};
    QVector<float> pending;
    qint64 analyzedSamples = 0, analyzedFrames = 0;
    QElapsedTimer lastSamples;
    lastSamples.start();
    int lastProgress = -1;
    while (!m_quit.load()) {
        QString failure;
        std::shared_ptr<const AudioClip> completed;
        int progressFrames = -1, totalFrames = 0;
        {
            QMutexLocker lock(&m_mutex);
            const bool formatChanged = m_rate != appliedRate || m_epoch != appliedEpoch;
            const quint64 currentOverruns = m_source->overrunCount();
            if ((formatChanged || currentOverruns != overruns) && m_recorded) {
                failure = formatChanged ? QStringLiteral("The audio device changed during recording. Please record the take again.")
                                        : QStringLiteral("Audio samples were dropped. Please record the take again.");
                m_recorded.reset();
            }
            overruns = currentOverruns;
            if (formatChanged && m_rate > 0 && m_rate <= 384000) {
                appliedRate = m_rate;
                appliedEpoch = m_epoch;
                analyzer.reset();
                analyzer.configure(appliedRate, dsp::kBandCount);
                m_history.fill(0, appliedRate * kWarmupFrames / kFps);
                m_historyCursor = m_historyFilled = 0;
                pending.clear();
                analyzedSamples = analyzedFrames = 0;
                state = {};
                m_frame = {};
            }
            if (appliedRate > 0 && m_source->ring()->available() > 0) {
                // A new epoch can arrive in the ring before its queued format
                // notification. Never combine samples from different rates.
                if (m_source->ring()->epoch() != appliedEpoch) {
                    m_source->ring()->reset();
                } else {
                    const int got = int(m_source->ring()->read(block.data(), block.size()));
                    lastSamples.restart();
                    for (int i = 0; i < got; ++i) {
                        const float value = std::isfinite(block[i]) ? block[i] : 0;
                        m_history[m_historyCursor] = value;
                        m_historyCursor = (m_historyCursor + 1) % m_history.size();
                        m_historyFilled = std::min(m_historyFilled + 1, m_history.size());
                        if (m_recorded) {
                            m_recorded->samples[m_recordCursor++] = value;
                            if (m_recordCursor == m_recorded->samples.size()) {
                                completed = std::move(m_recorded);
                                progressFrames = totalFrames = m_recordFrames;
                            }
                        }
                        pending.append(value);
                        const qint64 boundary = (analyzedFrames + 1) * appliedRate / kFps;
                        if (++analyzedSamples == boundary) {
                            analyzer.pushSamples(pending.constData(), quint32(pending.size()));
                            analyzer.analyze(state);
                            pending.clear();
                            ++analyzedFrames;
                            state.formatEpoch = appliedEpoch;
                            m_frame = state;
                        }
                    }
                }
            }
            if (m_recorded) {
                const int done = int((m_recordCursor - m_history.size()) * kFps / appliedRate);
                if (done != lastProgress) {
                    progressFrames = lastProgress = done;
                    totalFrames = m_recordFrames;
                }
                if (lastSamples.elapsed() > 3000) {
                    m_recorded.reset();
                    failure = QStringLiteral("No system audio samples are arriving. Check audio access and your output device, then retry.");
                }
            } else lastProgress = -1;
        }
        if (!failure.isEmpty()) Q_EMIT failed(failure);
        if (progressFrames >= 0) Q_EMIT progress(progressFrames, totalFrames);
        if (completed) Q_EMIT recorded(std::move(completed));
        QThread::msleep(4);
    }
}

} // namespace hyperwave::studio
