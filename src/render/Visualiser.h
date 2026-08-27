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

    // The pixel size this look needs at this style and scale.
    virtual QSize pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const = 0;

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
};

} // namespace hyperwave
