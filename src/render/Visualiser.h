#pragma once

#include <QImage>
#include <QSize>

#include "dsp/FrameState.h"
#include "render/RenderStyle.h"

namespace hyperwave {

// One analysed frame in, one image out.
//
// The interface exists so a second look can be added without a second copy of
// everything around it. Every surface -- status item, notch panel, window,
// eventually a Windows tray icon -- keeps a Visualiser rather than a concrete
// renderer, so the choice of look is a line in App and nothing else changes.
//
// Deliberately still QImage. The pixels have to reach a macOS status item, a
// tray icon and a frameless panel, and a QImage becomes all three. That
// constraint is what rules out handing a GPU surface back from here, and it is
// why a visualiser that wants three dimensions projects them itself rather
// than asking for a context.
class Visualiser
{
public:
    virtual ~Visualiser() = default;

    // How wide this look wants to be, in points.
    //
    // The status item is sized from this, so a look that is not a strip does
    // not have to sit in the middle of one. A ring is as wide as it is tall and
    // asking for 91 points of menu bar to draw a circle in the middle of would
    // be taking space to leave it empty.
    virtual qreal widthPt(const RenderStyle &style, qreal heightPt) const
    {
        Q_UNUSED(heightPt);
        return style.stripWidthPt();
    }

    // The pixel size this look needs at this style and scale. Derived from
    // widthPt() rather than restated, so the buffer and the item it goes into
    // cannot disagree about how wide the look is.
    virtual QSize pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const
    {
        return QSize(qMax(1, qRound(widthPt(style, heightPt) * dpr)),
                     qMax(1, qRound(heightPt * dpr)));
    }

    // `target` must be ARGB32_Premultiplied and already sized to pixelSize().
    // Implementations fill it and must never resize it: on macOS the buffer
    // goes to Core Animation without a copy, and reallocating under a live
    // layer is how you get tearing.
    virtual void render(const FrameState &state, const RenderStyle &style, qreal heightPt,
                        qreal dpr, QImage &target) = 0;

    // A visualiser that accumulates history has to be told when the stream it
    // was accumulating is no longer the same one -- a source change, a format
    // change, a surface resize. The default is to have nothing to forget.
    virtual void reset() {}

    // Offline capture supplies a frame time; ordinary presenters keep using
    // the renderer's clock. Negative values restore the live clock.
    virtual void setAnimationTime(qreal seconds) { Q_UNUSED(seconds); }
};

} // namespace hyperwave
