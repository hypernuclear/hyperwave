#pragma once

#include <vector>

#include "render/Visualiser.h"

namespace hyperwave {

// Upright strokes across the width, each lit by one band.
//
// Deliberately the plainest renderer in the project. What makes the reference
// image striking is not the lines -- they are rectangles -- it is the bloom and
// the lens dispersion that happen to them afterwards, and that is a post-process
// every look shares. So this draws rectangles and gets out of the way.
//
// The one thing it does care about is that brightness, not height, carries the
// signal. A row of strokes that all reach the full height and differ only in how
// hard they burn reads as light behind a screen; the same strokes varying in
// height read as a bar chart, which is the look the bars already are.
class LineRenderer : public Visualiser
{
public:
    QSize pixelSize(const RenderStyle &style, qreal heightPt, qreal dpr) const override;
    void render(const FrameState &state, const RenderStyle &style, qreal heightPt, qreal dpr,
                QImage &target) override;

private:
    std::vector<float> m_smoothed;
};

} // namespace hyperwave
