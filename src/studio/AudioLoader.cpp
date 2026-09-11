#include "studio/AudioLoader.h"

#include <cmath>
#include <QAudioBuffer>
#include <QUrl>

namespace hyperwave::studio {

AudioLoader::AudioLoader(QObject *parent) : QObject(parent)
{
    connect(&m_decoder, &QAudioDecoder::bufferReady, this, &AudioLoader::readBuffer);
    connect(&m_decoder, &QAudioDecoder::finished, this, [this] {
        if (!m_loading) return;
        if (m_clip->samples.isEmpty()) {
            fail(QStringLiteral("This file contains no decodable audio."));
            return;
        }
        m_loading = false;
        Q_EMIT loaded(m_clip);
    });
    connect(&m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
            [this](QAudioDecoder::Error) {
                if (m_loading) fail(m_decoder.errorString());
            });
}

void AudioLoader::cancel()
{
    m_loading = false;
    m_decoder.stop();
    m_clip.reset();
}

void AudioLoader::load(const QString &path)
{
    cancel();
    m_clip = std::make_shared<AudioClip>();
    m_clip->path = path;
    m_loading = true;
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    m_decoder.setAudioFormat(format);
    m_decoder.setSource(QUrl::fromLocalFile(path));
    m_decoder.start();
}

void AudioLoader::fail(const QString &message)
{
    cancel();
    Q_EMIT failed(message);
}

void AudioLoader::readBuffer()
{
    if (!m_loading) return;
    const QAudioBuffer buffer = m_decoder.read();
    if (!buffer.isValid()) return;
    const QAudioFormat format = buffer.format();
    if (!format.isValid() || (!m_clip->samples.isEmpty()
                              && m_clip->sampleRate != format.sampleRate())) {
        fail(QStringLiteral("The audio format changed while decoding. Try a WAV file."));
        return;
    }
    m_clip->sampleRate = format.sampleRate();
    if (m_clip->samples.size() + buffer.frameCount() > qint64(m_clip->sampleRate) * 600) {
        fail(QStringLiteral("Choose an audio file no longer than ten minutes."));
        return;
    }
    const char *data = buffer.constData<char>();
    for (int frame = 0; frame < buffer.frameCount(); ++frame) {
        float mono = 0;
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            const float sample = format.normalizedSampleValue(data);
            if (std::isfinite(sample)) mono += sample / format.channelCount();
            data += format.bytesPerSample();
        }
        m_clip->samples.append(mono);
    }
}

} // namespace hyperwave::studio
