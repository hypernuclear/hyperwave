#include "studio/RenderSession.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <QColorSpace>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>

#include "render/BarRenderer.h"
#include "render/LineRenderer.h"
#include "render/MirrorRenderer.h"
#include "render/Palette.h"
#include "render/RidgeRenderer.h"
#include "render/RingRenderer.h"
#include "render/StrandRenderer.h"

namespace hyperwave::studio {
namespace {
std::unique_ptr<Visualiser> makeRenderer(const QString &id)
{
    if (id == QLatin1String("mirror")) return std::make_unique<MirrorRenderer>();
    if (id == QLatin1String("lines")) return std::make_unique<LineRenderer>();
    if (id == QLatin1String("ring")) return std::make_unique<RingRenderer>();
    if (id == QLatin1String("ridge")) return std::make_unique<RidgeRenderer>();
    if (id == QLatin1String("strands")) {
        if (!StrandRenderer::isAvailable())
            throw std::runtime_error("Strands needs a supported GPU. Choose another visualizer.");
        return std::make_unique<StrandRenderer>();
    }
    return std::make_unique<BarRenderer>();
}

float demoSample(qint64 sample, int rate)
{
    // A repeatable musical bed across the spectrum, analysed by the real DSP.
    constexpr double tau = 6.283185307179586;
    const double t = double(sample) / rate;
    const double beat = t * 2.0 - std::floor(t * 2.0);
    double value = 0.20 * std::exp(-beat * 10.0) * std::sin(tau * 65.4 * t);
    for (int k = 0; k < 12; ++k) {
        const double frequency = 110.0 * std::pow(1.49, k);
        const double envelope = 0.5 + 0.5 * std::sin(t * (0.8 + k * 0.11) + k * 1.9);
        value += 0.045 * envelope * std::sin(tau * frequency * t);
    }
    return float(value);
}
} // namespace

QStringList visualisers()
{
    return {QStringLiteral("bars"), QStringLiteral("mirror"), QStringLiteral("lines"),
            QStringLiteral("ring"), QStringLiteral("strands"), QStringLiteral("ridge")};
}

QString layoutId(Layout layout)
{
    switch (layout) {
    case Layout::MenuBar: return QStringLiteral("menu-bar");
    case Layout::NotchCollapsed: return QStringLiteral("notch-collapsed");
    case Layout::NotchExpanded: return QStringLiteral("notch-expanded");
    }
    return {};
}

RenderStyle::Bloom defaultBloom(const QString &id)
{
    RenderStyle::Bloom bloom;
    bloom.enabled = id == QLatin1String("lines") || id == QLatin1String("ring");
    return bloom;
}

QSizeF nativeSize(const CaptureSettings &s)
{
    if (s.layout == Layout::NotchCollapsed) return {185, 52};
    if (s.layout == Layout::NotchExpanded) return {377, 106};
    if (s.visualiser == QLatin1String("ring")) return {22, 22};
    RenderStyle style = s.style;
    style.bandCount = s.compact ? 14 : dsp::kBandCount;
    return {style.stripWidthPt(), 22};
}

QString validate(const CaptureSettings &s, const AudioClip *clip)
{
    if (!visualisers().contains(s.visualiser)) return QStringLiteral("Unknown visualizer.");
    if (s.size.width() < 16 || s.size.height() < 16 || s.size.width() > 8192
        || s.size.height() > 8192 || qint64(s.size.width()) * s.size.height() > 16777216)
        return QStringLiteral("Choose dimensions from 16 to 8192 px, up to 16 megapixels.");
    if (s.frames < 1 || s.frames > kFps * 300)
        return QStringLiteral("Choose a duration between one frame and five minutes.");
    if (!std::isfinite(s.startSeconds) || s.startSeconds < 0 || s.startSeconds > 600)
        return QStringLiteral("Start time must be between 0 and 600 seconds.");
    if (clip && (clip->samples.isEmpty() || clip->sampleRate <= 0))
        return QStringLiteral("The audio file contains no usable samples.");
    if (clip && s.startSeconds + double(s.frames) / kFps > clip->duration() + 0.00001)
        return QStringLiteral("The selected range extends beyond the audio file. Shorten it or move the start.");
    return {};
}

bool savePng(const QImage &image, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    // PNG stores straight alpha. Explicit conversion avoids depending on the
    // image plugin's handling of the renderers' premultiplied buffers.
    QImage straight = image.convertToFormat(QImage::Format_RGBA8888);
    straight.setColorSpace(QColorSpace::SRgb);
    QImageWriter writer(&file, "png");
    if (straight.isNull() || !writer.write(straight)) {
        if (error) *error = writer.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

RenderSession::RenderSession(CaptureSettings settings, std::shared_ptr<const AudioClip> clip)
    : m_settings(std::move(settings)), m_clip(std::move(clip))
{
    const QString error = validate(m_settings, m_clip.get());
    if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
    m_renderer = makeRenderer(m_settings.visualiser);
    m_style = m_settings.style;
    m_style.bandCount = m_settings.compact ? 14 : dsp::kBandCount;
    m_nativeSize = nativeSize(m_settings);
    if (m_settings.layout != Layout::MenuBar) {
        const bool expanded = m_settings.layout == Layout::NotchExpanded;
        m_height = expanded ? 54 : 20;
        m_style.bandCount = dsp::kBandCount;
        m_style.drawShadow = false;
        m_style.insetTopPt = expanded ? 4 : 2;
        m_style.insetBottomPt = expanded ? 5 : 3;
        const qreal available = m_nativeSize.width() - 28;
        m_style.barWidthPt = std::clamp(available / (2 * m_style.bandCount - 1), 2.0, 7.0);
        m_style.barGapPt = m_style.barWidthPt;
        m_style.peakRadiusPt = std::max(1.0, m_style.barWidthPt * 0.45);
        if (expanded) m_style.minBarPt = m_style.barWidthPt;
        const qreal content = (2 * m_style.bandCount - 1) * m_style.barWidthPt;
        m_style.insetLeftPt = m_style.insetRightPt = std::max(4.0, (available - content) / 2);
    }
    Palette::applyTo(m_style, m_settings.gradient, Qt::white);
    m_style.idleOpacity = dsp::kIdlePresence;
    m_scale = std::min(m_settings.size.width() / m_nativeSize.width(),
                       m_settings.size.height() / m_nativeSize.height());
    m_strip = QImage(m_renderer->pixelSize(m_style, m_height, m_scale),
                     QImage::Format_ARGB32_Premultiplied);
    if (m_strip.isNull()) throw std::runtime_error("Could not allocate the render buffer.");
    m_sampleRate = m_clip ? m_clip->sampleRate : 48000;
    m_analyzer.configure(m_sampleRate, m_style.bandCount);
    m_sample = qint64(std::floor((m_settings.startSeconds - double(kWarmupFrames) / kFps)
                                 * m_sampleRate));
}

QImage RenderSession::step()
{
    // Rational sample boundaries support 44.1 kHz and other rates without
    // accumulating timing error. Before the audio starts, feed actual silence.
    const double time = m_settings.startSeconds + double(m_frame) / kFps;
    const qint64 end = qint64(std::floor((m_settings.startSeconds
                                         + double(m_frame + 1) / kFps) * m_sampleRate));
    QVector<float> samples;
    samples.reserve(int(end - m_sample));
    for (; m_sample < end; ++m_sample) {
        const float sample = m_clip
            ? (m_sample >= 0 && m_sample < m_clip->samples.size() ? m_clip->samples[m_sample] : 0)
            : demoSample(m_sample, m_sampleRate);
        samples.append(sample);
    }
    m_analyzer.pushSamples(samples.constData(), quint32(samples.size()));
    m_analyzer.analyze(m_state);
    ++m_frame;
    return render(m_state, time + double(kWarmupFrames) / kFps);
}

QImage RenderSession::renderLive(const FrameState &input, double seconds)
{
    FrameState state = input;
    if (m_style.bandCount < input.bandCount) {
        state.bandCount = m_style.bandCount;
        for (int band = 0; band < state.bandCount; ++band) {
            const int begin = band * input.bandCount / state.bandCount;
            const int end = (band + 1) * input.bandCount / state.bandCount;
            state.bars[band] = state.peaks[band] = 0;
            for (int i = begin; i < end; ++i) {
                state.bars[band] = std::max(state.bars[band], input.bars[i]);
                state.peaks[band] = std::max(state.peaks[band], input.peaks[i]);
            }
        }
    }
    return render(state, std::max(0.0, seconds));
}

QImage RenderSession::render(const FrameState &state, double seconds)
{
    m_strip.fill(Qt::transparent);
    m_renderer->setAnimationTime(seconds);
    m_renderer->render(state, m_style, m_height, m_scale, m_strip);
    m_post.apply(m_strip, m_style.bloom);

    QImage canvas(m_settings.size, QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull()) throw std::runtime_error("Could not allocate the output image.");
    canvas.fill(Qt::transparent);
    canvas.setColorSpace(QColorSpace::SRgb);
    QPainter p(&canvas);
    const QSizeF fitted = m_nativeSize * m_scale;
    const QPointF origin((canvas.width() - fitted.width()) / 2,
                         (canvas.height() - fitted.height()) / 2);
    if (m_settings.includePanel && m_settings.layout != Layout::MenuBar) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(origin);
        p.scale(m_scale, m_scale);
        QPainterPath shape;
        shape.setFillRule(Qt::WindingFill);
        shape.addRoundedRect(QRectF(QPointF(), m_nativeSize), 14, 14);
        shape.addRect(0, 0, m_nativeSize.width(), 14);
        p.fillPath(shape.simplified(), Qt::black);
        p.restore();
    }
    const qreal y = m_settings.layout == Layout::MenuBar ? 0
        : m_settings.layout == Layout::NotchExpanded ? 42 : 32;
    p.drawImage(origin + QPointF((fitted.width() - m_strip.width()) / 2, y * m_scale), m_strip);
    return canvas;
}

} // namespace hyperwave::studio
