#pragma once

#include <QAudioDecoder>
#include "studio/RenderSession.h"

namespace hyperwave::studio {

class AudioLoader : public QObject
{
    Q_OBJECT
public:
    explicit AudioLoader(QObject *parent = nullptr);
    void load(const QString &path);
    void cancel();
    bool loading() const { return m_loading; }
Q_SIGNALS:
    void loaded(std::shared_ptr<const AudioClip> clip);
    void failed(const QString &message);
private:
    void readBuffer();
    void fail(const QString &message);
    QAudioDecoder m_decoder;
    std::shared_ptr<AudioClip> m_clip;
    bool m_loading = false;
};

} // namespace hyperwave::studio
