#include <cmath>

#include <QImage>
#include <QTest>

#include "dsp/FrameState.h"
#include "render/RenderStyle.h"
#include "render/RidgeRenderer.h"

using namespace hyperwave;

namespace {

// Where the terrain actually put ink, as a margin in pixels from each edge.
//
// Measured off the alpha channel rather than asserted against the projection's
// arithmetic, because the arithmetic is the thing under test: the bug this
// pins down was a centring calculation that looked right and drew off-centre.
struct Margins
{
    int left = -1;
    int right = -1;
    bool empty() const { return left < 0; }
};

Margins inkMargins(const QImage &image)
{
    Margins margins;
    for (int x = 0; x < image.width(); ++x) {
        bool lit = false;
        for (int y = 0; y < image.height() && !lit; ++y)
            lit = qAlpha(image.pixel(x, y)) > 8;
        if (!lit)
            continue;
        if (margins.left < 0)
            margins.left = x;
        margins.right = image.width() - 1 - x;
    }
    return margins;
}

// A magnified canvas on purpose. At the 91pt the status item actually uses,
// one point is two pixels and a real asymmetry rounds away to nothing.
constexpr qreal kHeightPt = 22.0;
constexpr qreal kDpr = 6.0;

RenderStyle terrainStyle()
{
    RenderStyle style;
    // The renderer draws in flat white and colours the frame in one SourceIn
    // pass at the end. Left at the default invalid colours that pass erases
    // everything it just drew, and the measurement finds an empty image.
    style.gradientTop = Qt::white;
    style.gradientBottom = Qt::white;
    return style;
}

QImage renderTerrain(const RenderStyle &style, int frames)
{
    RidgeRenderer renderer;
    QImage target(renderer.pixelSize(style, kHeightPt, kDpr), QImage::Format_ARGB32_Premultiplied);

    FrameState state;
    state.bandCount = style.bandCount;
    state.presence = 1.0f;   // full opacity, so the far rows are measurable too

    // Enough frames to fill the history: the projection's horizontal extent is
    // taken over the rows it is about to draw, so a terrain one row deep and a
    // terrain sixty rows deep are different arrangements of the same maths.
    for (int frame = 0; frame < frames; ++frame) {
        state.seq = static_cast<std::uint32_t>(frame + 1);
        for (int band = 0; band < state.bandCount; ++band) {
            state.bars[band] =
                0.5f + 0.4f * std::sin(0.7f * static_cast<float>(band) + 0.3f * frame);
        }
        renderer.render(state, style, kHeightPt, kDpr, target);
    }
    return target;
}

} // namespace

class TstRidge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The terrain sits centred in the strip it is given.
    //
    // It did not. The shear is applied as (z - 0.5) so that it is centred on
    // depth, but depth also sets a row's width: the nearest row is the widest
    // and takes the whole negative half of the shear, while the rows taking the
    // positive half have been narrowed by perspective. Measured at 6x on a 91pt
    // strip, that put the terrain 14px off the left edge of the canvas and left
    // 94px of empty strip on the right.
    void centredInTheStrip()
    {
        const QImage image = renderTerrain(terrainStyle(), 70);
        const Margins margins = inkMargins(image);

        QVERIFY(!margins.empty());
        // Clipping first: an overhang reads as a small margin on that side and
        // would otherwise be indistinguishable from being merely off-centre.
        QVERIFY2(margins.left > 0 && margins.right > 0,
                 qPrintable(QStringLiteral("terrain touches the canvas edge: left=%1 right=%2")
                                .arg(margins.left)
                                .arg(margins.right)));
        QVERIFY2(std::abs(margins.left - margins.right) <= 1,
                 qPrintable(QStringLiteral("terrain is off centre: left=%1 right=%2")
                                .arg(margins.left)
                                .arg(margins.right)));
    }

    // The same, over the parameters the centring is derived from.
    //
    // shearX is the one that broke it, and its sign matters: with a negative
    // shear the widest row is displaced the other way, so the extremes of the
    // extent are not necessarily at the ends of the depth range. depth decides
    // how much narrower the far rows are, which is the other half of why
    // centred in z is not centred in x.
    void centredAcrossShearAndDepth_data()
    {
        QTest::addColumn<qreal>("shearX");
        QTest::addColumn<qreal>("depth");

        // Kept to combinations the strip can actually hold. A shallow depth
        // leaves the far rows nearly as wide as the near ones, and a hard shear
        // then pushes the two apart until the terrain is wider than the canvas.
        // That case clips, and clipping is its own test below.
        for (const qreal shear : {-0.2, -0.1, 0.0, 0.1, 0.2}) {
            for (const qreal depth : {1.0, 4.0, 9.0}) {
                QTest::addRow("shear %.1f depth %.1f", shear, depth) << shear << depth;
            }
        }
    }

    void centredAcrossShearAndDepth()
    {
        QFETCH(qreal, shearX);
        QFETCH(qreal, depth);

        RenderStyle style = terrainStyle();
        style.ridge.shearX = shearX;
        style.ridge.depth = depth;
        // Fewer rows than the default, and fewer frames to fill them. The
        // extent is taken over whatever rows are being drawn, so the property
        // holds at any history depth, and fifteen cases at sixty-four rows is
        // fifteen seconds of test for no extra coverage.
        style.ridge.rows = 24;

        const Margins margins = inkMargins(renderTerrain(style, 30));

        QVERIFY(!margins.empty());
        QVERIFY2(margins.left > 0 && margins.right > 0,
                 qPrintable(QStringLiteral("terrain touches the canvas edge: left=%1 right=%2")
                                .arg(margins.left)
                                .arg(margins.right)));
        QVERIFY2(std::abs(margins.left - margins.right) <= 1,
                 qPrintable(QStringLiteral("terrain is off centre: left=%1 right=%2")
                                .arg(margins.left)
                                .arg(margins.right)));
    }

    // A terrain with no history yet is drawn from a single row, which is the
    // one case where the extent is taken over one z rather than a range. It is
    // also what the first frame after a source change looks like.
    // Asking for more width than the strip has is allowed. Losing it off one
    // end is not.
    //
    // A shallow depth leaves the far rows nearly as wide as the near ones, and
    // a hard shear then pushes the two apart past the edges of the canvas.
    // Centred, that clips the same amount off both ends. The bug this file
    // exists for clipped one end and left the other empty, which is what an
    // off-centre terrain looks like once it stops fitting.
    void clipsEquallyWhenWiderThanTheStrip()
    {
        RenderStyle style = terrainStyle();
        style.ridge.shearX = 0.3;
        style.ridge.depth = 0.5;
        style.ridge.rows = 24;

        const Margins margins = inkMargins(renderTerrain(style, 30));

        QVERIFY(!margins.empty());
        QCOMPARE(margins.left, margins.right);
    }

    void centredOnTheFirstFrame()
    {
        const Margins margins = inkMargins(renderTerrain(terrainStyle(), 1));

        QVERIFY(!margins.empty());
        QVERIFY2(std::abs(margins.left - margins.right) <= 1,
                 qPrintable(QStringLiteral("first frame is off centre: left=%1 right=%2")
                                .arg(margins.left)
                                .arg(margins.right)));
    }
};

QTEST_APPLESS_MAIN(TstRidge)
#include "tst_ridge.moc"
