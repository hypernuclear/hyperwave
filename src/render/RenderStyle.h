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

    // --- the ridgeline look -------------------------------------------------
    //
    // Kept here with everything else because a look is data, not a code path.
    // These are ignored by the bar renderer and vice versa; carrying both in
    // one struct is cheaper than a variant and keeps App's style code single.
    struct Ridge
    {
        // Time slices held in the terrain. Forty at a row per frame is two
        // thirds of a second of history at 60 fps: long enough to watch a
        // phrase arrive, short enough that the back of the terrain is still
        // related to what you are hearing.
        //
        // This is also the dominant cost. Halving it very nearly halves the
        // frame, which no other knob here does, so it is the first thing to
        // reach for and the last thing to raise.
        int rows = 40;
        int frameStride = 1;

        // Antialiasing is the single most expensive thing this look does, and
        // with forty overlapping translucent ridges it buys less than it does
        // for a lone curve -- the terrain is its own antialiasing. Measured
        // both ways before choosing.
        bool antialias = true;

        // Lines the other way, every Nth sample, which is what turns a stack of
        // separate curves into one surface you can read the shape of. Zero
        // turns them off. They are drawn after the ridges and fainter, so they
        // suggest the mesh rather than competing with it.
        int columnStride = 8;
        qreal columnOpacity = 0.35;

        // Interpolation between bands, Catmull-Rom. Twenty-eight points across
        // a strip is visibly faceted; three samples per band turns the ridges
        // into curves for about a fifth of the frame.
        int subdivision = 3;

        // Perspective. depth is how much further the oldest row is than the
        // newest, as a multiple of the eye distance, so 1.6 means the back of
        // the terrain is 2.6x away and draws at 38% scale.
        qreal depth = 1.6;
        qreal spreadX = 0.92;    // fraction of the width the nearest row spans
        qreal amplitude = 0.55;  // fraction of the height a full-scale band rises
        qreal baseline = 0.88;   // where the nearest row sits, 0 = top
        qreal horizon = 0.30;    // where infinitely far rows converge to
        // A little lateral slide with depth. Pure symmetric convergence reads
        // as a diagram; an oblique one reads as an object.
        qreal shearX = 0.10;
        // How fast older rows fade out. Above 1 the falloff is gentle at the
        // front and steep at the back, which keeps the near ridges legible
        // without the far ones turning into a solid mass.
        qreal fadeExponent = 1.5;
        qreal lineWidthPt = 0.7;
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

        qreal radius = 0.10;      // resting radius of the tube, model units
        // Ribbon width in points, converted to clip units against the target
        // height before it reaches the shader. The original expressed this in
        // clip space, which is fine for a full window and useless here: 0.01 of
        // a 44px-tall strip is a quarter of a pixel, which is nothing at all.
        qreal thicknessPt = 0.6;
        // How much of the height the tube fills. The model radius is small so
        // that the noise octaves stay in a sane range; this scales the whole
        // bundle up to the target afterwards.
        qreal fill = 1.8;
        // Tilt, in degrees, so the tube reads as a solid object rather than a
        // flat ribbon that happens to wobble.
        qreal tiltDegrees = 18.0;
        qreal twists = 0.5;       // turns along the length -- the finger trap
        qreal opacity = 0.5;

        // The spectrum arrives 0..1 already, but a strip that has to read at a
        // glance wants the quiet parts pushed up. Applied before clamping.
        qreal audioGain = 1.0;

        // Radians per second the bundle turns. Slow, because this is ambient.
        qreal spinRate = 0.35;
    };
    Strands strands;

    qreal stripWidthPt() const
    {
        return insetLeftPt + insetRightPt
             + bandCount * barWidthPt + (bandCount - 1) * barGapPt;
    }
};

} // namespace hyperwave
