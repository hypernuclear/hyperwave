#pragma once

#include <vector>

#include <QLinearGradient>
#include <QPointF>

#include "dsp/DspConfig.h"
#include "render/Visualiser.h"

namespace hyperwave {

// A spectrogram terrain: frequency across, magnitude up, time receding.
//
// Each time slice becomes one polyline drawn across the bands, and the slices
// stack away from the viewer under perspective, so what you see is a ridge of
// mountains sliding backwards as it ages. It is the same analysis the bars
// draw -- the difference is that this one remembers.
//
// Projected in software rather than on the GPU, which deserves a defence
// because "3D" usually implies otherwise. The targets are a 118x22 pt status
// item and a notch panel a few hundred points wide; at that size the terrain is
// forty polylines of about eighty points, and QPainter draws it in 1.0 ms --
// a sixteenth of a 60 fps frame. A GPU path would need a context and a
// framebuffer per surface and a readback every frame to get back to the QImage
// that the status item, the tray icon and the panel all require: more
// machinery and more latency than the drawing costs. It would start to pay
// with an order of magnitude more geometry or real additive blending, and at
// that point the projection below is the part that stays.
//
// It is not free, and the honest number is not the wall-clock one. Measured
// end to end against the bars in a RelWithDebInfo build with a synthetic
// source: 19.6% of a core against 7.4%. Wall time per frame is only 1.0 ms
// because Qt's raster engine spreads the stroking across its thread pool, so
// the summed CPU is roughly double what a stopwatch around render() suggests.
// Both numbers are true and they answer different questions -- the frame is
// never late, and the battery still notices. RenderStyle::Ridge carries the
// knobs: rows dominates the cost, antialias is worth 2.3x on its own, and
// column lines are nearly free.
class RidgeRenderer : public Visualiser
{
public:
    QSize pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const override;
    void render(const FrameState &state, const RenderStyle &style, qreal heightPt, qreal dpr,
                QImage &target) override;
    void reset() override;

private:
    void pushRow(const FrameState &state, const RenderStyle &style);

    // History as a ring, newest at m_head. Fixed capacity so a frame never
    // allocates: the renderer runs on the UI thread sixty times a second and
    // the one thing it must not do is stall.
    static constexpr int kMaxRows = 96;
    float m_rows[kMaxRows][dsp::kMaxBandCount] = {};
    int m_head = 0;
    int m_filled = 0;
    int m_sinceRow = 0;
    std::uint32_t m_lastSeq = 0;
    std::int32_t m_lastBandCount = 0;

    // Scratch for one projected polyline, reused across rows and frames.
    std::vector<QPointF> m_points;

    QLinearGradient m_gradient;
    QColor m_cachedTop, m_cachedBottom;
    int m_cachedWidth = -1;
};

} // namespace hyperwave
