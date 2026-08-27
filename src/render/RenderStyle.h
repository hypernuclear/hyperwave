#pragma once

#include <QColor>

namespace hyperwave {

// Everything the renderer needs to know about how the strip should look,
// in points. The renderer multiplies by the device pixel ratio exactly once,
// at paint time, which is what makes 1x / 2x / 3x correct by construction
// rather than by a scattering of fudge factors.
struct RenderStyle
{
    int bandCount = 28;

    qreal barWidthPt = 2.0;
    qreal barGapPt = 2.0;
    qreal insetLeftPt = 4.0;
    qreal insetRightPt = 4.0;

    // Vertical inset from the status item's height. Tuned by looking at it: at
    // 5pt a side the bars used barely half the available height and read as
    // undersized next to Apple's own glyphs, which are considerably chunkier
    // than they feel. 3pt leaves the strip clearly inside the bar without
    // making it timid.
    qreal insetTopPt = 3.0;
    qreal insetBottomPt = 3.5;

    qreal peakRadiusPt = 1.0;
    qreal peakGapPt = 1.5;   // clearance between a bar's cap and its dot

    // A bar at zero still shows a dot of this height, so silence degrades into
    // an even row rather than a ragged disappearance.
    qreal minBarPt = 2.0;

    QColor gradientTop;
    QColor gradientBottom;
    QColor peakColor;

    // Legibility insurance on a translucent menu bar. macOS 26 composites the
    // bar over the wallpaper, so a saturated gradient that sings over a dark
    // desktop can vanish over a bright one. A tight dark halo under each bar
    // costs almost nothing and rescues the light case.
    bool drawShadow = true;
    QColor shadowColor;
    qreal shadowBlurPt = 1.5;

    // Resting opacity when nothing is playing. Never zero: a menu bar item that
    // disappears reads as a crash.
    qreal idleOpacity = 0.35;

    qreal stripWidthPt() const
    {
        return insetLeftPt + insetRightPt
             + bandCount * barWidthPt + (bandCount - 1) * barGapPt;
    }
};

} // namespace hyperwave
