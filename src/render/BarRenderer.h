#pragma once

#include <vector>

#include <QImage>
#include <QLinearGradient>
#include <QRectF>
#include <QSize>

#include "dsp/FrameState.h"
#include "render/RenderStyle.h"
#include "render/Visualiser.h"

namespace hyperwave {

// Turns one analysed frame into one image. This is the only place in the
// project that draws anything.
//
// QPainter rather than Core Graphics or a stack of CALayers, for one reason:
// the same pixels have to reach a macOS status item, a Windows tray icon
// (which is a bitmap HICON) and a frameless strip. A QImage becomes all three
// trivially; a CALayer tree becomes none of them. The cost is about 30-70 us
// per frame, which is a fraction of a percent of one core at 60 fps -- a fair
// price for having exactly one renderer instead of three that drift apart.
class BarRenderer : public Visualiser
{
public:
    // The pixel size a strip of this style needs at this scale. Static because
    // callers ask before they have a renderer to ask; the virtual override
    // below is the same answer through the interface.
    static QSize sizeFor(const RenderStyle &style, qreal stripHeightPt, qreal dpr);

    QSize pixelSize(const RenderStyle &style, qreal stripHeightPt, qreal dpr) const override
    {
        return sizeFor(style, stripHeightPt, dpr);
    }

    // `target` must be ARGB32_Premultiplied and already sized to pixelSize().
    // The renderer fills it; it never resizes it, because on macOS the buffer
    // is handed to Core Animation without a copy and reallocating it under a
    // live layer is how you get tearing.
    void render(const FrameState &state, const RenderStyle &style, qreal stripHeightPt,
                qreal dpr, QImage &target) override;

private:
    void rebuildCache(const RenderStyle &style, qreal stripHeightPt);

    QLinearGradient m_gradient;
    QColor m_cachedTop, m_cachedBottom;
    qreal m_cachedHeight = -1.0;
    bool m_cachedRainbow = false;

    // Bar geometry, kept across frames so the shadow and fill passes are drawing
    // exactly the same shapes, and so a small vector is not reallocated sixty
    // times a second.
    std::vector<QRectF> m_geometry;
};

} // namespace hyperwave
