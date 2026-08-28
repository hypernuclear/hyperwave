#include "render/LineRenderer.h"

#include "render/Palette.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "dsp/DspConfig.h"

namespace hyperwave {


void LineRenderer::render(const FrameState &state, const RenderStyle &style, qreal heightPt,
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

    const RenderStyle::Lines &l = style.lines;
    const int bands = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
    const int count = std::clamp(l.count, 1, 512);

    // Brightness eases rather than jumping.
    //
    // The ballistics upstream are tuned for bars, where a fast attack reads as
    // punch. Here a stroke that snaps to full brightness and back strobes,
    // because the bloom multiplies the change: it is the difference between a
    // light coming on and a light flickering.
    if (static_cast<int>(m_smoothed.size()) != count)
        m_smoothed.assign(static_cast<std::size_t>(count), 0.0f);

    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, false);   // upright edges, no need
    p.setPen(Qt::NoPen);

    const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
    const qreal opacity = style.idleOpacity + (1.0 - style.idleOpacity) * presence;

    const qreal inset = h * l.verticalInset;
    const qreal top = inset;
    const qreal bottom = h - inset;

    const qreal strokeW = std::max(1.0, l.widthPt * dpr);
    const qreal spacing = static_cast<qreal>(w) / static_cast<qreal>(count + 1);

    for (int i = 0; i < count; ++i) {
        // Spread the bands across the strokes. With more strokes than bands the
        // spectrum is interpolated, which keeps a dense wall of lines from
        // showing obvious repeats.
        const qreal u = count > 1 ? static_cast<qreal>(i) / (count - 1) : 0.0;
        const qreal pos = u * (bands - 1);
        const int i0 = std::clamp(static_cast<int>(pos), 0, bands - 1);
        const int i1 = std::clamp(i0 + 1, 0, bands - 1);
        const float t = static_cast<float>(pos - i0);
        const float level = state.bars[i0] * (1.0f - t) + state.bars[i1] * t;

        float &smooth = m_smoothed[static_cast<std::size_t>(i)];
        const float rate = level > smooth ? static_cast<float>(l.attack)
                                          : static_cast<float>(l.release);
        smooth += (level - smooth) * rate;

        // A floor, so an unlit stroke is still a faint line rather than a gap.
        // The reference has this too: the quiet strokes are visible, they simply
        // are not burning.
        const qreal lit = l.floorLevel + (1.0 - l.floorLevel) * std::pow(smooth, l.gamma);

        // Height varies a little with level as well, but only a little: the
        // signal is carried by brightness.
        const qreal shrink = (1.0 - l.heightResponse) + l.heightResponse * lit;
        const qreal centre = (top + bottom) * 0.5;
        const qreal half = (bottom - top) * 0.5 * shrink;

        // Position across the strip, so a stroke keeps its hue as it flashes
        // rather than changing colour with its own level.
        QColor colour = style.rainbow
            ? Palette::rainbowAt(count > 1 ? static_cast<qreal>(i) / (count - 1) : 0.0)
            : style.gradientBottom;
        // Towards white as a stroke burns, which is what gives the bloom
        // something achromatic to spread and makes the hot ones read as light
        // rather than as a saturated colour.
        const qreal toWhite = std::pow(lit, 1.3);
        colour.setRedF(colour.redF() + (1.0 - colour.redF()) * toWhite);
        colour.setGreenF(colour.greenF() + (1.0 - colour.greenF()) * toWhite);
        colour.setBlueF(colour.blueF() + (1.0 - colour.blueF()) * toWhite);
        colour.setAlphaF(std::clamp<qreal>(opacity * lit, 0.0, 1.0));

        const qreal x = std::round(spacing * (i + 1) - strokeW * 0.5);
        p.fillRect(QRectF(x, centre - half, strokeW, half * 2.0), colour);
    }
}

} // namespace hyperwave
