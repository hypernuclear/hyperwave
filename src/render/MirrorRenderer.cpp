#include "render/MirrorRenderer.h"

#include "render/Palette.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "dsp/DspConfig.h"

namespace hyperwave {


void MirrorRenderer::reset()
{
    std::fill(m_value.begin(), m_value.end(), 0.0f);
    std::fill(m_velocity.begin(), m_velocity.end(), 0.0f);
}

void MirrorRenderer::render(const FrameState &state, const RenderStyle &style, qreal heightPt,
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

    const RenderStyle::Mirror &m = style.mirror;
    const int bands = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
    const int count = std::clamp(m.count, 1, 512);

    if (static_cast<int>(m_value.size()) != count) {
        m_value.assign(static_cast<std::size_t>(count), 0.0f);
        m_velocity.assign(static_cast<std::size_t>(count), 0.0f);
    }

    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
    const qreal opacity = style.idleOpacity + (1.0 - style.idleOpacity) * presence;
    p.setOpacity(opacity);

    const qreal centreY = h * 0.5;
    const qreal maxHalf = h * 0.5 * (1.0 - m.verticalInset);
    const qreal strokeW = std::max(1.0, m.widthPt * dpr);
    const qreal spacing = static_cast<qreal>(w) / static_cast<qreal>(count);
    const qreal radius = strokeW * 0.5;

    const auto stiffness = static_cast<float>(m.stiffness);
    const auto damping = static_cast<float>(m.damping);

    // Already left to right, so the rainbow only has to replace the stops.
    QLinearGradient gradient(0.0, 0.0, static_cast<qreal>(w), 0.0);
    if (style.rainbow) {
        Palette::addRainbowStops(gradient);
    } else {
        gradient.setColorAt(0.0, style.gradientBottom);
        gradient.setColorAt(1.0, style.gradientTop);
    }
    p.setBrush(gradient);

    for (int i = 0; i < count; ++i) {
        const qreal u = count > 1 ? static_cast<qreal>(i) / (count - 1) : 0.0;
        const qreal pos = u * (bands - 1);
        const int i0 = std::clamp(static_cast<int>(pos), 0, bands - 1);
        const int i1 = std::clamp(i0 + 1, 0, bands - 1);
        const float t = static_cast<float>(pos - i0);
        const float level = state.bars[i0] * (1.0f - t) + state.bars[i1] * t;

        // A damped spring, integrated once per frame.
        //
        // Underdamped on purpose: the value overshoots its target and settles
        // back, which is the bounce. Critically damped would arrive and stop,
        // which is what an eased interpolation already does and what this look
        // is specifically not meant to feel like. Velocity is clamped so a
        // transient cannot fling a stroke off the strip and back.
        float &x = m_value[static_cast<std::size_t>(i)];
        float &v = m_velocity[static_cast<std::size_t>(i)];
        v += (level - x) * stiffness;
        v *= damping;
        v = std::clamp(v, -0.5f, 0.5f);
        x = std::clamp(x + v, 0.0f, 1.4f);

        const qreal half = std::max<qreal>(m.minHalfPt * dpr, x * maxHalf);
        const qreal x0 = spacing * (i + 0.5) - strokeW * 0.5;
        p.drawRoundedRect(QRectF(x0, centreY - half, strokeW, half * 2.0), radius, radius);
    }
}

} // namespace hyperwave
