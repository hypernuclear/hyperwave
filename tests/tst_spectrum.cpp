#include <cmath>
#include <random>
#include <vector>

#include <QTest>

#include "dsp/DspConfig.h"
#include "dsp/SpectrumAnalyzer.h"

using namespace hyperwave;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

// Runs the analyser long enough for the ballistics and the AGC to settle, then
// hands back the last frame. The analyser is a stateful filter, so a single
// frame from a cold start tells you almost nothing.
FrameState settle(SpectrumAnalyzer &a, const std::function<float(int)> &sample, int frames)
{
    const int hop = static_cast<int>(kRate / dsp::kFrameHz);
    std::vector<float> block(static_cast<std::size_t>(hop));
    FrameState state;
    int t = 0;
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < hop; ++i)
            block[static_cast<std::size_t>(i)] = sample(t++);
        a.pushSamples(block.data(), static_cast<std::uint32_t>(hop));
        a.analyze(state);
    }
    return state;
}

int loudestBand(const FrameState &s)
{
    int best = 0;
    for (int k = 1; k < s.bandCount; ++k) {
        if (s.bars[k] > s.bars[best])
            best = k;
    }
    return best;
}

} // namespace

class TstSpectrum : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowTracksSampleRate()
    {
        // The window is a constant ~85 ms rather than a constant sample count,
        // so the display behaves the same at 44.1 as at 96.
        SpectrumAnalyzer a;
        a.configure(48000.0, dsp::kBandCount);
        QCOMPARE(a.fftSize(), 4096);
        a.configure(96000.0, dsp::kBandCount);
        QCOMPARE(a.fftSize(), 8192);
        a.configure(44100.0, dsp::kBandCount);
        QCOMPARE(a.fftSize(), 4096);
    }

    void sineLandsInTheRightBand_data()
    {
        QTest::addColumn<double>("freq");
        QTest::newRow("100 Hz") << 100.0;
        QTest::newRow("1 kHz") << 1000.0;
        QTest::newRow("4 kHz") << 4000.0;
        QTest::newRow("10 kHz") << 10000.0;
    }

    void sineLandsInTheRightBand()
    {
        QFETCH(double, freq);

        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        const FrameState s = settle(a, [freq](int t) {
            return static_cast<float>(0.4 * std::sin(2.0 * kPi * freq * t / kRate));
        }, 240);

        // Work out which band should own this frequency from the same
        // geometric spacing the mapper uses.
        const double ratio = dsp::kFreqHi / dsp::kFreqLo;
        const int expected = static_cast<int>(
            std::floor(std::log(freq / dsp::kFreqLo) / std::log(ratio) * dsp::kBandCount));

        const int got = loudestBand(s);
        QVERIFY2(std::abs(got - expected) <= 1,
                 qPrintable(QStringLiteral("%1 Hz peaked in band %2, expected ~%3")
                                .arg(freq).arg(got).arg(expected)));
    }

    // The test that catches summing versus averaging.
    //
    // Log-spaced bands have bandwidth proportional to f and white noise has
    // constant power density, so summing gives a display that RISES with
    // frequency for white noise -- and pink noise, which falls as 1/f, is the
    // signal that comes out flat. Asserting on pink is what pins the choice
    // down: average the bins instead and this slopes hard downwards.
    void pinkNoiseGivesRoughlyFlatBands()
    {
        std::mt19937 rng(4242);
        std::normal_distribution<float> gauss(0.0f, 0.25f);

        // Voss-McCartney-ish: a one-pole cascade approximating -3 dB/octave.
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        auto pink = [&](int) {
            const float white = gauss(rng);
            b0 = 0.99765f * b0 + white * 0.0990460f;
            b1 = 0.96300f * b1 + white * 0.2965164f;
            b2 = 0.57000f * b2 + white * 1.0526913f;
            return (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
        };

        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        const FrameState s = settle(a, pink, 300);

        // Ignore the extreme ends: the bottom bands are narrower than a bin and
        // the top ones sit against the 18 kHz analysis cut.
        float lo = 1.0f, hi = 0.0f;
        for (int k = 4; k < s.bandCount - 4; ++k) {
            lo = std::min(lo, s.bars[k]);
            hi = std::max(hi, s.bars[k]);
        }
        QVERIFY2(hi > 0.15f, "pink noise should light the display up at all");
        QVERIFY2(hi - lo < 0.45f,
                 qPrintable(QStringLiteral("pink noise spread %1..%2 across bands; "
                                           "a summed log-band display should be roughly flat")
                                .arg(lo).arg(hi)));
    }

    void silenceRestsOnTheFloor()
    {
        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        const FrameState s = settle(a, [](int) { return 0.0f; }, 300);

        for (int k = 0; k < s.bandCount; ++k)
            QVERIFY2(s.bars[k] < 0.02f, "silence must not light any band");
        QCOMPARE(s.presence, 0.0f);
        QVERIFY(s.rmsDb <= -120.0f);
        QVERIFY(a.exactZeroFrames() > 0);
    }

    // Digital silence and a tap we are not allowed to hear look identical here,
    // which is exactly why the permission probe needs a second, independent
    // signal before it accuses anyone of anything.
    void exactZeroFramesResetOnRealAudio()
    {
        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        settle(a, [](int) { return 0.0f; }, 120);
        QVERIFY(a.exactZeroFrames() > 0);

        settle(a, [](int t) {
            return static_cast<float>(0.2 * std::sin(2.0 * kPi * 440.0 * t / kRate));
        }, 120);
        QCOMPARE(a.exactZeroFrames(), 0);
    }

    void presenceRisesWithAudioAndFallsWithSilence()
    {
        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);

        const FrameState loud = settle(a, [](int t) {
            return static_cast<float>(0.3 * std::sin(2.0 * kPi * 440.0 * t / kRate));
        }, 120);
        QCOMPARE(loud.presence, 1.0f);

        const FrameState quiet = settle(a, [](int) { return 0.0f; }, 300);
        QCOMPARE(quiet.presence, 0.0f);
    }

    // A quiet track still has to move, or the app looks broken on anything not
    // mastered to modern loudness.
    void agcLiftsAQuietSignal()
    {
        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        const FrameState s = settle(a, [](int t) {
            return static_cast<float>(0.02 * std::sin(2.0 * kPi * 1000.0 * t / kRate));
        }, 600);

        QVERIFY2(s.agcDb > 6.0f, "a -34 dBFS tone should have pulled the AGC up");
        QVERIFY2(s.bars[loudestBand(s)] > 0.3f, "and the display should be visibly moving");
    }

    void seqAdvancesEveryFrame()
    {
        SpectrumAnalyzer a;
        a.configure(kRate, dsp::kBandCount);
        const FrameState first = settle(a, [](int) { return 0.0f; }, 100);
        const FrameState second = settle(a, [](int) { return 0.0f; }, 1);
        QVERIFY(second.seq > first.seq);
    }
};

QTEST_APPLESS_MAIN(TstSpectrum)
#include "tst_spectrum.moc"
