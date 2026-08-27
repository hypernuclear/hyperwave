#include "render/RingRenderer.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
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
        // bands straight round the circle makes a lopsided blob.
        const qreal around = static_cast<qreal>(i) / segments;
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

    const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
    const qreal opacity = style.idleOpacity + (1.0 - style.idleOpacity) * presence;

    // A circle, not an ellipse.
    //
    // The radius comes from the shorter side, so the shape stays 1:1 on a
    // status item that is four times wider than it is tall. Stretching it to
    // fill the strip filled the space, but a squashed circle is not the shape
    // this look is: the protrusions stop reading as pushes outward from a
    // centre and start reading as bumps on a horizon.
    const qreal cx = w * 0.5;
    const qreal cy = h * 0.5;
    const qreal extent = std::min(w, h) * 0.5;
    const qreal base = extent * r.baseRadius;
    const qreal reach = extent * r.reach;

    const auto outline = [&](qreal amplitudeScale, qreal radialOffset, QPointF shift) {
        for (int i = 0; i <= segments; ++i) {
            const int k = i % segments;
            const qreal theta = kTau * static_cast<qreal>(k) / segments - kTau * 0.25;
            const qreal push = m_value[static_cast<std::size_t>(k)] * amplitudeScale;
            const qreal radius = base + reach * push + radialOffset;
            m_points[static_cast<std::size_t>(i)] =
                QPointF(cx + std::cos(theta) * radius + shift.x(),
                        cy + std::sin(theta) * radius + shift.y());
        }
    };

    // The disc, drawn once from the true outline, before any of the copies.
    {
        outline(1.0, 0.0, QPointF(0.0, 0.0));
        QPainterPath path;
        path.moveTo(m_points[0]);
        for (int i = 1; i <= segments; ++i)
            path.lineTo(m_points[static_cast<std::size_t>(i)]);
        path.closeSubpath();
        QColor fill = style.gradientBottom;
        fill.setAlphaF(std::clamp<qreal>(opacity * r.fillOpacity, 0.0, 1.0));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(path);
    }

    // Five copies, offset like a mis-registered print rather than blurred.
    //
    // Cyan, magenta and yellow because that is what separates in a real
    // four-colour process, and because they stay distinguishable when they
    // overlap in a way that red, green and blue do not: overlapping CMY makes
    // darker mixtures towards the middle, which is what makes the fringes look
    // like layers rather than like a rainbow gradient. White last, so the ring
    // proper sits on top and the colour reads as escaping from underneath it.
    const qreal off = extent * r.constantOffset;
    struct Layer
    {
        qreal amplitude;
        QPointF shift;
        QColor colour;
    };
    const Layer layers[5] = {
        {1.0 + r.separation, QPointF(-off, -off * 0.6), QColor(0, 200, 255)},
        {1.0 - r.separation, QPointF(off, -off * 0.6), QColor(255, 0, 190)},
        {1.0 + r.separation * 0.5, QPointF(0.0, off), QColor(255, 220, 0)},
        {1.0 - r.separation * 0.5, QPointF(0.0, -off * 0.4), QColor(150, 120, 255)},
        {1.0, QPointF(0.0, 0.0), QColor(255, 255, 255)},
    };

    p.setBrush(Qt::NoBrush);
    for (const Layer &layer : layers) {
        outline(layer.amplitude, 0.0, layer.shift);
        QColor c = layer.colour;
        c.setAlphaF(std::clamp<qreal>(opacity * r.layerOpacity, 0.0, 1.0));
        QPen pen(c, std::max(1.0, r.lineWidthPt * dpr));
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(m_points.data(), segments + 1);
    }
}

} // namespace hyperwave
