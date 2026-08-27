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

    // One point, which is as tight as this can go and stay crisp.
    //
    // Bars sit at insetLeft + k * (width + gap) and are scaled by the device
    // pixel ratio at paint time, so every one of those edges has to land on a
    // whole device pixel or the raster engine antialiases the vertical sides
    // and the row goes soft. With a 1pt gap the stride is 3pt, which is 3px at
    // 1x, 6px at 2x and 9px at 3x -- integers everywhere. 0.5pt would be half a
    // pixel on a non-Retina display, and 1.5pt would be one and a half; both
    // look fine on this laptop and blurry on someone's external monitor, which
    // is the worst way for a thing like this to be wrong.
    qreal barGapPt = 1.0;
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

    // --- the ridgeline look -------------------------------------------------
    //
    // Kept here with everything else because a look is data, not a code path.
    // These are ignored by the bar renderer and vice versa; carrying both in
    // one struct is cheaper than a variant and keeps App's style code single.
    struct Ridge
    {
        // Time slices held in the terrain. Ten at a row per frame is about a
        // sixth of a second of history at 60 fps -- short, deliberately: a long
        // tail turned the shape into a solid mass, and what reads best is a few
        // distinct ridges you can watch travel backwards.
        //
        // This is also the dominant cost. Halving it very nearly halves the
        // frame, which no other knob here does, so it is the first thing to
        // reach for and the last thing to raise.
        int rows = 64;
        int frameStride = 1;

        // Antialiasing is the single most expensive thing this look does, and
        // it matters more now than it did at forty rows: with only ten ridges
        // there is far less overlap to hide a stair-stepped edge. Measured both
        // ways before choosing.
        bool antialias = true;

        // Lines the other way, every Nth sample, which is what turns a stack of
        // separate curves into one surface you can read the shape of. Zero
        // turns them off. They are drawn after the ridges and fainter, so they
        // suggest the mesh rather than competing with it.
        int columnStride = 1;
        qreal columnOpacity = 0.35;

        // Interpolation between bands, Catmull-Rom. Twenty-eight points across
        // a strip is visibly faceted; four samples per band turns the ridges
        // into curves, and with ten rows the extra points are affordable.
        int subdivision = 2;

        // Perspective. depth is how much further the oldest row is than the
        // newest, as a multiple of the eye distance, so 1.9 means the back of
        // the terrain is 2.9x away and draws at 34% scale.
        qreal depth = 4.0;
        qreal spreadX = 0.92;    // fraction of the width the nearest row spans
        qreal amplitude = 0.93;  // fraction of the height a full-scale band rises
        qreal baseline = 0.88;   // where the nearest row sits, 0 = top
        qreal horizon = 0.30;    // where infinitely far rows converge to
        // A little lateral slide with depth. Pure symmetric convergence reads
        // as a diagram; an oblique one reads as an object.
        qreal shearX = 0.10;
        // How fast older rows fade out. Above 1 the falloff is gentle at the
        // front and steep at the back, which keeps the near ridges legible
        // without the far ones turning into a solid mass.
        qreal fadeExponent = 1.5;
        qreal lineWidthPt = 0.58;
    };
    Ridge ridge;

    // --- the strands look ----------------------------------------------------
    //
    // A bundle of lines wrapped around an axis, radius driven by the spectrum
    // and by noise. Drawn on the GPU, because that is what the technique is:
    // the geometry is static and all of the motion is a vertex shader.
    struct Strands
    {
        // How many strands in the bundle. Tunable rather than a constant: it is
        // the one parameter that trades density against cost, and the right
        // number is not the same for a 118pt strip and a panel. Instanced, so
        // raising it makes the draw bigger rather than making more draws.
        int count = 64;

        // Points along each strand, expanded to two vertices each. Uploaded
        // once at startup and never touched again.
        int segments = 200;

        qreal radius = 0.12;      // resting radius of the tube, model units
        // Ribbon width in points, converted to clip units against the target
        // height before it reaches the shader. The original expressed this in
        // clip space, which is fine for a full window and useless here: 0.01 of
        // a 44px-tall strip is a quarter of a pixel, which is nothing at all.
        qreal thicknessPt = 0.8;
        // How much of the height the tube fills. The model radius is small so
        // that the noise octaves stay in a sane range; this scales the whole
        // bundle up to the target afterwards.
        qreal fill = 3.55;
        // Rotation of the bundle about its own long axis, in degrees.
        //
        // Named "tilt" and it is not one, which is worth stating rather than
        // leaving the name to imply something the code does not do. A strand
        // point is (x, r*cos(phi), r*sin(phi)); the transform rotates about X,
        // giving screen_y = fill * r * cos(phi + theta); and z is then thrown
        // away, because the projection carries no perspective term -- w stays
        // 1 -- and there is no depth test. So rotating about X is algebraically
        // a phase offset added to the angle: the same thing spinRate does,
        // without the animation.
        //
        // Verified rather than reasoned: rendered at 0, 30, 60 and 90 degrees
        // with the spin stopped, and the silhouette, the vertical extent and
        // the density are the same in all four. What changes is which strand
        // sits where and which slice of the noise field shows.
        //
        // Still a useful knob, because re-phasing the noise changes the shape.
        // It simply cannot lean the tube towards or away from the viewer; that
        // would need a perspective term in the projection.
        qreal tiltDegrees = 60.4;
        qreal twists = 1.38;       // turns along the length -- the finger trap
        qreal opacity = 1.0;

        // The spectrum arrives 0..1 already, but a strip that has to read at a
        // glance wants the quiet parts pushed up. Applied before clamping.
        qreal audioGain = 0.87;

        // Radians per second the bundle turns.
        qreal spinRate = 1.16;
    };
    Strands strands;

    qreal stripWidthPt() const
    {
        return insetLeftPt + insetRightPt
             + bandCount * barWidthPt + (bandCount - 1) * barGapPt;
    }
};

} // namespace hyperwave
