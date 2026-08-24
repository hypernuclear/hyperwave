#include <cmath>
#include <vector>

#include <QTest>

#include "dsp/Ballistics.h"
#include "dsp/DspConfig.h"

using namespace hyperlight;

namespace {

// One band, stepped by hand. The ballistics are a pure filter with no audio
// dependency, so they can be driven deterministically a frame at a time --
// which is the whole reason they live in their own class.
struct Rig
{
    Ballistics b;
    float bars[dsp::kMaxBandCount] = {};
    float peaks[dsp::kMaxBandCount] = {};

    void step(float levelDb, float rmsDb, int frames = 1)
    {
        std::vector<float> levels(dsp::kBandCount, dsp::kFloorDb);
        levels[0] = levelDb;
        for (int i = 0; i < frames; ++i)
            b.process(levels.data(), dsp::kBandCount, rmsDb, bars, peaks);
    }
};

// Level in dB that maps to a given 0..1 bar height, before AGC.
float dbForU(float u)
{
    return dsp::kFloorDb + u * (dsp::kCeilDb - dsp::kFloorDb);
}

} // namespace

class TstBallistics : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Pins the exponential exactly rather than eyeballing "about 63% after one
    // time constant". At 60 Hz a 25 ms tau is a frame and a half, so the
    // discrete steps dominate and any continuous-time approximation is noise.
    //
    // RMS below the AGC freeze threshold keeps the gain pinned at 0 dB, which
    // makes the arithmetic here closed-form: band 0 sees 0.85 of full scale
    // (its own weight plus the edge-clamped left neighbour), and the bar
    // approaches that as 1 - a^n.
    void attackFollowsTheExponentialExactly()
    {
        const float a = dsp::coeff(dsp::kAttackMs);
        const float ceiling = dsp::kSmoothCentre + dsp::kSmoothSide;   // edge clamp

        for (int n : {1, 2, 3, 5, 10}) {
            Rig r;
            r.step(dbForU(1.0f), -100.0f, n);
            const float expected = ceiling * (1.0f - std::pow(a, static_cast<float>(n)));
            QVERIFY2(std::abs(r.bars[0] - expected) < 1e-4f,
                     qPrintable(QStringLiteral("after %1 frames bar was %2, expected %3")
                                    .arg(n).arg(r.bars[0]).arg(expected)));
        }
    }

    // And the property that actually matters at the display: a transient is
    // most of the way up within a couple of frames.
    void attackIsFastEnoughToReadAsPunch()
    {
        Rig r;
        r.step(dbForU(1.0f), -100.0f, 3);
        const float ceiling = dsp::kSmoothCentre + dsp::kSmoothSide;
        QVERIFY2(r.bars[0] > 0.8f * ceiling,
                 "a bar should be most of the way up within 50 ms");
    }

    void releaseIsSlowerThanAttack()
    {
        // Not just "release decays" -- the point of the two constants is that
        // the bar snaps up and eases down. If they ever get swapped the display
        // looks lethargic on transients and twitchy on decays.
        QVERIFY(dsp::kReleaseMs > dsp::kAttackMs);

        Rig up;
        up.step(dbForU(1.0f), -20.0f, 200);
        const float top = up.bars[0];
        QVERIFY(top > 0.5f);

        up.step(dsp::kFloorDb, -20.0f, 1);
        const float afterOneRelease = up.bars[0];

        Rig down;
        down.step(dbForU(0.0f), -20.0f, 1);
        QVERIFY2(top - afterOneRelease < top * 0.2f,
                 "one frame of release should barely move a settled bar");
    }

    void peakHoldsThenFalls()
    {
        Rig r;
        r.step(dbForU(1.0f), -20.0f, 200);
        const float held = r.peaks[0];
        QVERIFY(held > 0.5f);

        // Drop the input and watch the dot sit still for the hold window.
        const int holdFrames = static_cast<int>(dsp::kPeakHoldMs / dsp::kFrameMs);
        r.step(dsp::kFloorDb, -20.0f, holdFrames - 2);
        QVERIFY2(std::abs(r.peaks[0] - held) < 1e-4f, "peak should not move during its hold");

        const float before = r.peaks[0];
        r.step(dsp::kFloorDb, -20.0f, 20);
        QVERIFY2(r.peaks[0] < before, "peak should fall once the hold expires");
    }

    // The dot accelerates rather than descending at constant speed. That is
    // most of the character of the whole thing, so it is worth pinning.
    void peakFallAccelerates()
    {
        Rig r;
        r.step(dbForU(1.0f), -20.0f, 200);
        const int holdFrames = static_cast<int>(dsp::kPeakHoldMs / dsp::kFrameMs);
        r.step(dsp::kFloorDb, -20.0f, holdFrames + 1);

        const float p0 = r.peaks[0];
        r.step(dsp::kFloorDb, -20.0f, 5);
        const float p1 = r.peaks[0];
        r.step(dsp::kFloorDb, -20.0f, 5);
        const float p2 = r.peaks[0];

        const float firstDrop = p0 - p1;
        const float secondDrop = p1 - p2;
        QVERIFY2(secondDrop > firstDrop,
                 qPrintable(QStringLiteral("drops were %1 then %2; the fall should speed up")
                                .arg(firstDrop).arg(secondDrop)));
    }

    void peakNeverSinksBelowItsBar()
    {
        Rig r;
        r.step(dbForU(1.0f), -20.0f, 300);
        for (int k = 0; k < dsp::kBandCount; ++k)
            QVERIFY(r.peaks[k] >= r.bars[k] - 1e-5f);
    }

    void agcFreezesInSilence()
    {
        // Otherwise the gain would ramp all the way up chasing the noise floor,
        // and the first quiet moment of the next track would explode.
        Rig r;
        r.step(dsp::kFloorDb, -100.0f, 600);
        QCOMPARE(r.b.agcDb(), 0.0f);
    }

    void agcRisesForQuietAndFallsForLoud()
    {
        Rig quiet;
        quiet.step(dbForU(0.15f), -40.0f, 900);
        QVERIFY2(quiet.b.agcDb() > 3.0f, "a quiet input should have pulled gain up");

        Rig loud;
        loud.step(dbForU(1.0f), -6.0f, 900);
        QVERIFY2(loud.b.agcDb() < quiet.b.agcDb(), "a loud input should sit lower");
    }

    void agcStaysWithinItsBounds()
    {
        Rig veryQuiet;
        veryQuiet.step(dsp::kFloorDb + 1.0f, -50.0f, 3000);
        QVERIFY(veryQuiet.b.agcDb() <= dsp::kAgcMaxDb + 1e-3f);
        QVERIFY(veryQuiet.b.agcDb() >= dsp::kAgcMinDb - 1e-3f);
    }

    void presenceHoldsBeforeFading()
    {
        Rig r;
        r.step(dbForU(1.0f), -20.0f, 60);
        QCOMPARE(r.b.presence(), 1.0f);

        // Silence inside the hold window must not start the fade -- a gap
        // between tracks should not make the strip flinch.
        const int holdFrames = static_cast<int>(dsp::kSilenceHoldMs / dsp::kFrameMs);
        r.step(dsp::kFloorDb, -100.0f, holdFrames - 2);
        QCOMPARE(r.b.presence(), 1.0f);

        r.step(dsp::kFloorDb, -100.0f, 120);
        QCOMPARE(r.b.presence(), 0.0f);
    }

    void resetClearsEverything()
    {
        Rig r;
        r.step(dbForU(1.0f), -20.0f, 200);
        QVERIFY(r.bars[0] > 0.1f);

        r.b.reset();
        r.step(dsp::kFloorDb, -100.0f, 1);
        QCOMPARE(r.b.presence(), 0.0f);
        QCOMPARE(r.b.agcDb(), 0.0f);
        QVERIFY(r.bars[0] < 0.01f);
    }
};

QTEST_APPLESS_MAIN(TstBallistics)
#include "tst_ballistics.moc"
