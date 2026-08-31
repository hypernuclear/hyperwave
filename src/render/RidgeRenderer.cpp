#include "render/RidgeRenderer.h"

#include "render/Palette.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>
#include <QPen>

namespace hyperwave {

namespace {

// Catmull-Rom through four samples. Used to put curves between the bands
// rather than corners: a spectrum is a sampled continuum, so interpolating it
// is closer to the truth than the polyline through the samples is.
float catmullRom(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f
         * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float sampleRow(const float *row, int count, float position)
{
    const int i = static_cast<int>(std::floor(position));
    const float t = position - static_cast<float>(i);
    const auto at = [row, count](int index) {
        return row[std::clamp(index, 0, count - 1)];
    };
    return catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

} // namespace


void RidgeRenderer::reset()
{
    m_head = 0;
    m_filled = 0;
    m_sinceRow = 0;
    m_lastSeq = 0;
    for (auto &row : m_rows)
        std::fill(std::begin(row), std::end(row), 0.0f);
}

void RidgeRenderer::pushRow(const FrameState &state, const RenderStyle &style)
{
    // A band count change means the previous rows measured something else.
    // Interpolating between two different band layouts produces a terrain that
    // is smooth, plausible and wrong.
    if (state.bandCount != m_lastBandCount) {
        reset();
        m_lastBandCount = state.bandCount;
    }

    if (++m_sinceRow < std::max(1, style.ridge.frameStride))
        return;
    m_sinceRow = 0;

    m_head = (m_head + 1) % kMaxRows;
    float *row = m_rows[m_head];
    const int count = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
    for (int b = 0; b < count; ++b)
        row[b] = state.bars[b];
    m_filled = std::min(m_filled + 1, kMaxRows);
}

void RidgeRenderer::render(const FrameState &state, const RenderStyle &style, qreal heightPt,
                           qreal dpr, QImage &target)
{
    if (target.isNull())
        return;

    if (state.seq != m_lastSeq) {
        pushRow(state, style);
        m_lastSeq = state.seq;
    }

    target.fill(Qt::transparent);
    // The device pixel ratio is applied here, exactly once, by working in
    // pixels throughout. Setting it on the image as well and scaling the
    // painter is how the bar renderer once ended up drawing at 4x.
    target.setDevicePixelRatio(1.0);

    const int w = target.width();
    const int h = target.height();
    if (w <= 0 || h <= 0)
        return;

    const RenderStyle::Ridge &r = style.ridge;
    const int rows = std::clamp(r.rows, 1, kMaxRows);
    const int bands = std::clamp(state.bandCount, 1, dsp::kMaxBandCount);
    const int subdivision = std::max(1, r.subdivision);
    const int samples = (bands - 1) * subdivision + 1;

    if (m_cachedWidth != w || m_cachedTop != style.gradientTop
        || m_cachedBottom != style.gradientBottom) {
        // Horizontal, so the colour runs along each ridge rather than with
        // height. A pen can take a gradient brush, which is what lets one
        // drawPolyline carry the whole sweep instead of a segment per colour.
        m_gradient = QLinearGradient(0.0, 0.0, static_cast<qreal>(w), 0.0);
        m_gradient.setColorAt(0.0, style.gradientBottom);
        m_gradient.setColorAt(1.0, style.gradientTop);
        m_cachedWidth = w;
        m_cachedTop = style.gradientTop;
        m_cachedBottom = style.gradientBottom;
    }

    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, r.antialias);
    // Every line is drawn in flat white and the whole frame is coloured once at
    // the end, through CompositionMode_SourceIn.
    //
    // This started as a performance hypothesis -- that a pen carrying the
    // gradient as its brush was forcing a slow per-segment path -- and the
    // hypothesis was wrong: measured both ways, the two are within noise of
    // each other. The cost is the stroking itself, not the colour. It is kept
    // because it is the better arrangement anyway: the colour is decided once
    // for the frame instead of being rebuilt into a pen forty times, and a row's
    // alpha stays a property of the row rather than something baked into a
    // gradient that also has to carry the sweep.

    const qreal baselineY = h * r.baseline;
    const qreal horizonY = h * r.horizon;
    const qreal camHeight = baselineY - horizonY;   // so z=0, y=0 lands on the baseline
    const qreal amplitude = h * r.amplitude;
    const qreal halfWidth = w * 0.5;

    const qreal presence = std::clamp<qreal>(state.presence, 0.0, 1.0);
    const qreal opacity = style.idleOpacity + (1.0 - style.idleOpacity) * presence;

    QPen pen(Qt::white, std::max(0.6, r.lineWidthPt * dpr));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    const int drawn = std::min(rows, std::max(m_filled, 1));

    // Where the projection actually leaves its ink, horizontally.
    //
    // The shear is applied as (z - 0.5) so that it is centred on depth -- but
    // depth is also what sets a row's width. The nearest row is the widest and
    // takes the whole negative half of the shear, while the rows taking the
    // positive half have been narrowed by perspective, so centred in z is not
    // centred in x. Measured at 6x on a 91pt strip, the terrain overhung the
    // left edge by 14px and left 94px empty on the right; every other look is
    // symmetric to the pixel.
    //
    // Taken over the rows actually drawn rather than solved for the endpoints:
    // with a negative shearX the extremes are not necessarily at the ends, and
    // the HUD can set one live.
    qreal inkMin = std::numeric_limits<qreal>::max();
    qreal inkMax = std::numeric_limits<qreal>::lowest();
    for (int age = 0; age < drawn; ++age) {
        const qreal z = drawn > 1 ? static_cast<qreal>(age) / (drawn - 1) : 0.0;
        const qreal halfSpan = halfWidth * r.spreadX / (1.0 + z * r.depth);
        const qreal shear = (z - 0.5) * r.shearX * w;
        inkMin = std::min(inkMin, halfWidth - halfSpan + shear);
        inkMax = std::max(inkMax, halfWidth + halfSpan + shear);
    }
    // Applied inside project(), so it moves the ridges and the mesh lines
    // together and the two passes cannot disagree about where centre is.
    const qreal centreOffset = halfWidth - (inkMin + inkMax) * 0.5;

    // One projection, used by both passes, so a ridge and the mesh line
    // crossing it cannot disagree about where a point is.
    const auto project = [&](int age, int sample) {
        const float *row = m_rows[((m_head - age) % kMaxRows + kMaxRows) % kMaxRows];
        const qreal z = drawn > 1 ? static_cast<qreal>(age) / (drawn - 1) : 0.0;
        const qreal scale = 1.0 / (1.0 + z * r.depth);
        const qreal u = samples > 1 ? static_cast<qreal>(sample) / (samples - 1) : 0.0;
        const float magnitude = sampleRow(row, bands, static_cast<float>(u) * (bands - 1));
        // The shear is centred rather than one-sided: sliding every far row to
        // the right drags the whole terrain off centre and leaves a wedge of
        // dead space, which reads as a layout bug rather than as a viewpoint.
        const qreal shear = (z - 0.5) * r.shearX * w;
        return QPointF(centreOffset + halfWidth
                           + (u - 0.5) * 2.0 * halfWidth * r.spreadX * scale + shear,
                       horizonY + (camHeight - magnitude * amplitude) * scale);
    };
    const auto fadeAt = [&](int age) {
        const qreal z = drawn > 1 ? static_cast<qreal>(age) / (drawn - 1) : 0.0;
        return std::max(0.06, std::pow(1.0 - z, r.fadeExponent));
    };

    // Back to front. There is no depth buffer and no fill, so the painter's
    // algorithm is the whole of the hidden-surface handling -- which is also
    // why the ridges stay translucent and you can see the terrain through
    // itself, as intended.
    m_points.resize(static_cast<std::size_t>(samples));
    for (int age = drawn - 1; age >= 0; --age) {
        for (int i = 0; i < samples; ++i)
            m_points[static_cast<std::size_t>(i)] = project(age, i);

        const qreal scale = 1.0 / (1.0 + (drawn > 1 ? static_cast<qreal>(age) / (drawn - 1) : 0.0)
                                             * r.depth);
        p.setOpacity(opacity * fadeAt(age));
        pen.setWidthF(std::max(0.5, r.lineWidthPt * dpr * scale));
        if (style.rainbow) {
            // Depth IS the ramp here: the nearest ridge is the start of the
            // rainbow and each row behind it steps further along, so the
            // terrain recedes through the spectrum. One flat colour per row,
            // because a row is all at one depth.
            pen.setColor(Palette::rainbowAt(
                drawn > 1 ? static_cast<qreal>(age) / (drawn - 1) : 0.0));
        }
        p.setPen(pen);
        p.drawPolyline(m_points.data(), samples);
    }

    // The other direction. Each column runs from the newest row to the oldest,
    // so its own fade cannot be per-row; it takes the average, which is enough
    // to keep the far end from looking pasted on.
    if (r.columnStride > 0 && drawn > 1) {
        m_points.resize(static_cast<std::size_t>(drawn));
        pen.setWidthF(std::max(0.4, r.lineWidthPt * dpr * 0.7));
        p.setPen(pen);
        p.setOpacity(opacity * r.columnOpacity);
        for (int i = 0; i < samples; i += r.columnStride) {
            for (int age = 0; age < drawn; ++age)
                m_points[static_cast<std::size_t>(age)] = project(age, i);
            if (style.rainbow) {
                // A column runs from the nearest row to the furthest, so it
                // crosses the entire ramp and cannot be one colour. The
                // gradient is built along its own screen path rather than
                // straight down the image: the terrain is sheared and the
                // ridges have height, so "far" is not simply "higher up".
                QLinearGradient along(m_points.front(), m_points.back());
                Palette::addRainbowStops(along);
                pen.setBrush(along);
                p.setPen(pen);
            }
            p.drawPolyline(m_points.data(), drawn);
        }
    }
    // The colour, in one pass. SourceIn keeps what has been drawn as a mask and
    // replaces its colour with the gradient, so the sweep still runs along the
    // ridges without any line having had to know about it.
    //
    // Skipped entirely under the rainbow, where the lines have already been
    // coloured individually. This trick cannot do that job: it can only paint a
    // gradient across the image, and the rainbow's axis is depth, which is not
    // an axis of the image -- a near ridge and a far one overlap in x and y and
    // must still come out different colours.
    if (!style.rainbow) {
        p.setOpacity(1.0);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(0, 0, w, h, m_gradient);
    }
}

} // namespace hyperwave
