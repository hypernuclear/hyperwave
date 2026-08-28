#include "render/BarRenderer.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "render/Palette.h"
#include <QPainterPath>

namespace hyperwave {

QSize BarRenderer::sizeFor(const RenderStyle &style, qreal stripHeightPt, qreal dpr)
{
    // Ceil rather than round. Scaled display modes produce fractional backing
    // scales, and a buffer one pixel short of what the layer wants shows up as
    // a hairline of transparent along an edge.
    const int w = static_cast<int>(std::ceil(style.stripWidthPt() * dpr));
    const int h = static_cast<int>(std::ceil(stripHeightPt * dpr));
    return {std::max(w, 1), std::max(h, 1)};
}

void BarRenderer::rebuildCache(const RenderStyle &style, qreal stripHeightPt)
{
    const qreal top = style.insetTopPt;
    const qreal bottom = stripHeightPt - style.insetBottomPt;

    // ONE gradient across the whole strip, not one per bar.
    //
    // A per-bar gradient normalises the ramp to each bar's own height, so every
    // bar ends up cyan-tipped no matter how tall it is and the row reads as
    // noise. Anchoring the ramp to the strip means a short bar shows only the
    // violet end and a tall one runs the full ramp, so colour encodes height
    // and the twenty-eight bars resolve into a single object.
    if (style.rainbow) {
        // The one case where the ramp does NOT run with height. A rainbow
        // encodes position rather than loudness, so it runs left to right
        // across the strip and each bar keeps one hue -- which is what makes
        // the row read as a spectrum, with the same bar always the same colour
        // instead of every bar changing hue as it moves.
        m_gradient = QLinearGradient(QPointF(0.0, 0.0), QPointF(style.stripWidthPt(), 0.0));
        Palette::addRainbowStops(m_gradient);
    } else {
        m_gradient = QLinearGradient(QPointF(0.0, top), QPointF(0.0, bottom));
        m_gradient.setColorAt(0.0, style.gradientTop);
        m_gradient.setColorAt(1.0, style.gradientBottom);
    }

    m_cachedRainbow = style.rainbow;
    m_cachedTop = style.gradientTop;
    m_cachedBottom = style.gradientBottom;
    m_cachedHeight = stripHeightPt;
}

void BarRenderer::render(const FrameState &state, const RenderStyle &style,
                         qreal stripHeightPt, qreal dpr, QImage &target)
{
    if (target.isNull())
        return;

    if (m_cachedHeight != stripHeightPt || m_cachedTop != style.gradientTop
        || m_cachedBottom != style.gradientBottom || m_cachedRainbow != style.rainbow) {
        rebuildCache(style, stripHeightPt);
    }

    // QPainter silently applies an image's own devicePixelRatio on top of
    // whatever transform you set. Leaving it at the backing scale and then
    // scaling by dpr below multiplies the two: everything draws at 4x, the
    // baseline lands past the bottom edge, short bars vanish entirely, and the
    // gradient shows only its top third -- three symptoms, one cause. The
    // renderer owns the point-to-pixel conversion, so it owns this too.
    target.setDevicePixelRatio(1.0);
    target.fill(Qt::transparent);

    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    // From here on everything is in points. This single scale is what makes 2x
    // and 3x correct without a fudge factor anywhere else in this file.
    p.scale(dpr, dpr);
    p.setPen(Qt::NoPen);

    const int bands = std::min(state.bandCount, style.bandCount);
    if (bands <= 0)
        return;

    // Presence fades towards a resting opacity rather than to nothing.
    const qreal alpha = style.idleOpacity + (1.0 - style.idleOpacity) * state.presence;
    p.setOpacity(std::clamp<qreal>(alpha, 0.0, 1.0));

    const qreal baseline = stripHeightPt - style.insetBottomPt;
    const qreal usable = baseline - style.insetTopPt;
    const qreal w = style.barWidthPt;
    const qreal r = w * 0.5;

    // Geometry once, drawn twice.
    //
    // The obvious implementation accumulates all twenty-eight bars into a
    // single QPainterPath and fills that. Do not: benchmarked on this exact
    // 236x44 buffer, one path fill costs 200 us against 68 us for the same
    // twenty-eight shapes drawn individually, because a multi-subpath fill
    // drops out of Qt's fast rounded-rect route and scan-converts the whole
    // bounding box instead. At 60 fps that difference is 2.6% of a core versus
    // 0.8% -- the entire energy budget of a menu bar resident, spent on an
    // abstraction that saved four lines.
    //
    // The gradient brush, for the record, is free: a solid fill benchmarks the
    // same to within a microsecond.
    m_geometry.resize(static_cast<std::size_t>(bands));
    for (int k = 0; k < bands; ++k) {
        const qreal x = style.insetLeftPt + k * (w + style.barGapPt);
        const qreal h = std::max(style.minBarPt, static_cast<qreal>(state.bars[k]) * usable);
        m_geometry[static_cast<std::size_t>(k)] = QRectF(x, baseline - h, w, h);
    }

    // A tight dark halo under the bars. The menu bar on macOS 26 composites
    // over the wallpaper, so a saturated gradient that reads beautifully on a
    // dark desktop can all but vanish on a bright one.
    //
    // The alpha lives in the colour rather than in setOpacity(): a non-unity
    // painter opacity pushes the raster engine onto a general composite path
    // for every span, and it looks identical.
    if (style.drawShadow) {
        p.setBrush(style.shadowColor);
        p.translate(0.0, 0.35);
        for (const QRectF &bar : m_geometry)
            p.drawRoundedRect(bar, r, r);
        p.translate(0.0, -0.35);
    }

    p.setBrush(m_gradient);
    for (const QRectF &bar : m_geometry)
        p.drawRoundedRect(bar, r, r);

    // Peak dots last, on top, in the accent colour rather than the gradient so
    // they stay legible against the bar they are sitting above. Under the
    // rainbow a single accent cannot do that -- white reads as an error over
    // yellow and vanishes over nothing else -- so each dot takes its own bar's
    // hue, lifted towards white.
    p.setBrush(style.peakColor);
    for (int k = 0; k < bands; ++k) {
        if (style.rainbow) {
            const qreal u = bands > 1 ? static_cast<qreal>(k) / (bands - 1) : 0.0;
            p.setBrush(Palette::rainbowAt(u).lighter(155));
        }
        const qreal x = style.insetLeftPt + k * (w + style.barGapPt);
        const qreal barH = std::max(style.minBarPt,
                                    static_cast<qreal>(state.bars[k]) * usable);
        const qreal peakH = static_cast<qreal>(state.peaks[k]) * usable;

        // Suppress the dot while it is sitting on the cap of its own bar --
        // otherwise every idle bar wears a permanent hat and the row looks
        // cluttered rather than settled.
        if (peakH <= barH + style.peakGapPt)
            continue;

        const qreal cy = baseline - peakH - style.peakRadiusPt;
        p.drawEllipse(QPointF(x + r, cy), style.peakRadiusPt, style.peakRadiusPt);
    }
}

} // namespace hyperwave
