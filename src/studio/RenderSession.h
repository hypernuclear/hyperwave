#pragma once

#include <memory>
#include <QImage>
#include <QSizeF>
#include <QStringList>
#include <QVector>

#include "dsp/SpectrumAnalyzer.h"
#include "render/GpuPost.h"
#include "render/Visualiser.h"

namespace hyperwave::studio {

inline constexpr int kFps = 60;
inline constexpr int kWarmupFrames = 120;

struct AudioClip
{
    QVector<float> samples;
    int sampleRate = 48000;
    QString path;
    bool systemAudio = false;
    double duration() const { return double(samples.size()) / sampleRate; }
};

enum class Layout { MenuBar, NotchCollapsed, NotchExpanded };

struct CaptureSettings
{
    QString visualiser = QStringLiteral("bars");
    QString gradient = QStringLiteral("signature");
    Layout layout = Layout::MenuBar;
    QSize size = {1456, 352};
    int frames = 600;
    double startSeconds = 0;
    bool includePanel = false;
    bool compact = false;
    RenderStyle style;
};

QStringList visualisers();
QString layoutId(Layout layout);
RenderStyle::Bloom defaultBloom(const QString &visualiser);
QSizeF nativeSize(const CaptureSettings &settings);
QString validate(const CaptureSettings &settings, const AudioClip *clip = nullptr);
bool savePng(const QImage &image, const QString &path, QString *error);

// The preview and exporter use this same clock and audio-analysis path.
// Each step is exactly 1/60 s, irrespective of how long rendering takes.
class RenderSession
{
public:
    RenderSession(CaptureSettings settings, std::shared_ptr<const AudioClip> clip = {});
    QImage step();
    QImage renderLive(const FrameState &state, double seconds);
    int nextFrame() const { return m_frame; }
    const CaptureSettings &settings() const { return m_settings; }

private:
    QImage render(const FrameState &state, double seconds);
    CaptureSettings m_settings;
    std::shared_ptr<const AudioClip> m_clip;
    std::unique_ptr<Visualiser> m_renderer;
    SpectrumAnalyzer m_analyzer;
    FrameState m_state;
    GpuPost m_post;
    RenderStyle m_style;
    QSizeF m_nativeSize;
    QImage m_strip;
    qreal m_height = 22;
    qreal m_scale = 1;
    int m_frame = -kWarmupFrames;
    int m_sampleRate = 48000;
    qint64 m_sample = 0;
};

} // namespace hyperwave::studio
