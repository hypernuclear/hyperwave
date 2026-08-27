#include "render/RingRenderer.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPen>

#include "dsp/DspConfig.h"

namespace hyperwave {

namespace {
constexpr qreal kTau = 6.283185307179586;
}

QSize RingRenderer::pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const
{
    return QSize(std::lround(style.stripWidthPt() * dpr), std::lround(heightPt * dpr));
}

void RingRenderer::reset()
{
    std::fill(m_value.begin(), m_value.end(), 0.0f);
    std::fill(m_velocity.begin(), m_velocity.end(), 0.0f);
}

void RingRenderer::render(const FrameState &state, const RenderStyle &style, qreal heightPt,
                          qreal dpr, QImage &target)
{
    Q_UNUSED(heightPt);
    if (target.isNull())
        return;

    target.setDevicePixelRatio(1.0);
    target.fill(Qt::transparent);

    const int w = target.width();
    const int h = target.height();
    if (w <= 0 || h <= 0)
        return;

    const RenderStyle::Ring &r = style.ring;
    const int bands = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
    const int segments = std::clamp(r.segments, 12, 1024);

    if (static_cast<int>(m_value.size()) != segments) {
        m_value.assign(static_cast<std::size_t>(segments), 0.0f);
        m_velocity.assign(static_cast<std::size_t>(segments), 0.0f);
    }
    m_points.resize(static_cast<std::size_t>(segments) + 1);

    const auto stiffness = static_cast<float>(r.stiffness);
    const auto damping = static_cast<float>(r.damping);

    for (int i = 0; i < segments; ++i) {
        // Mirrored: the spectrum runs out along the first half of the ring and
        // back along the second, so the two sides always agree. Reading the
        // bands straight round the circle makes a lopsided blob that looks like
        // a bug rather than a shape.
        const qreal around = static_cast<qreal>(i) / segments;      // 0..1
        const qreal folded = around < 0.5 ? around * 2.0 : (1.0 - around) * 2.0;
        const qreal pos = folded * (bands - 1);
        const int i0 = std::clamp(static_cast<int>(pos), 0, bands - 1);
        const int i1 = std::clamp(i0 + 1, 0, bands - 1);
        const float t = static_cast<float>(pos - i0);
        const float level = state.bars[i0] * (1.0f - t) + state.bars[i1] * t;

        float &x = m_value[static_cast<std::size_t>(i)];
        float &v = m_velocity[static_cast<std::size_t>(i)];
        v += (level - x) * stiffness;
        v *= damping;
        v = std::clamp(v, -0.5f, 0.5f);
        x = std::clamp(x + v, 0.0f, 1.4f);
    }

    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);

    const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
    const qreal opacity = style.idleOpacity + (1.0 - style.idleOpacity) * presence;

    // An ellipse, not a circle. The status item is far wider than it is tall,
    // and a circle that fits its height leaves most of the strip empty; an
    // ellipse that fills the space is the same shape stretched, and the
    // protrusions still read.
    const qreal cx = w * 0.5;
    const qreal cy = h * 0.5;
    const qreal baseX = w * 0.5 * r.baseRadius;
    const qreal baseY = h * 0.5 * r.baseRadius;
    const qreal reachX = w * 0.5 * r.reach;
    const qreal reachY = h * 0.5 * r.reach;

    // Three copies at slightly different amplitudes. Offsetting the amplitude
    // rather than the position is the whole trick: at rest every copy traces
    // the same ellipse and the ring is one clean line, and the colours only
    // separate where the spectrum pushes it out.
    struct Layer
    {
        qreal scale;
        QColor colour;
    };
    const QColor warm(255, 90, 120);
    const QColor cool(90, 170, 255);
    const Layer layers[3] = {
        {1.0 + r.separation, warm},
        {1.0 - r.separation, cool},
        {1.0, QColor(255, 255, 255)},
    };

    for (const Layer &layer : layers) {
        for (int i = 0; i <= segments; ++i) {
            const int k = i % segments;
            const qreal theta = kTau * static_cast<qreal>(k) / segments - kTau * 0.25;
            const qreal push = m_value[static_cast<std::size_t>(k)] * layer.scale;
            m_points[static_cast<std::size_t>(i)] =
                QPointF(cx + std::cos(theta) * (baseX + reachX * push),
                        cy + std::sin(theta) * (baseY + reachY * push));
        }
        QColor c = layer.colour;
        c.setAlphaF(std::clamp<qreal>(opacity * r.layerOpacity, 0.0, 1.0));
        QPen pen(c, std::max(1.0, r.lineWidthPt * dpr));
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(m_points.data(), segments + 1);
    }
}

} // namespace hyperwave
