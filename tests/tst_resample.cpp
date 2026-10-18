#include <cmath>
#include <vector>

#include <QTest>

#include "audio/Resample.h"

using namespace hyperwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, double rate, int n)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(2.0 * kPi * hz * i / rate));
    return out;
}

// Rising zero crossings per second: a frequency estimate that needs no FFT.
double frequency(const std::vector<float> &x, double rate)
{
    int crossings = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        crossings += (x[i - 1] < 0.0f && x[i] >= 0.0f);
    return crossings * rate / static_cast<double>(x.size());
}

} // namespace

class TstResample : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unitStepIsACopy()
    {
        LinearResampler r;
        const std::vector<float> in = sine(440.0, 48000.0, 512);
        std::vector<float> out(1024);
        QCOMPARE(r.process(in.data(), 512, out.data(), 1024), 511u);
        for (int i = 0; i < 511; ++i)
            QCOMPARE(out[i], in[i]);
    }

    // The bug this exists for: a 96k mic in a 48k analysis. A 1 kHz tone has
    // to come out at 1 kHz, not 500 Hz, and at half the sample count.
    void downsamplingKeepsThePitch()
    {
        LinearResampler r;
        r.setStep(96000.0 / 48000.0);
        const std::vector<float> in = sine(1000.0, 96000.0, 96000);
        std::vector<float> out(60000);
        const std::uint32_t got = r.process(in.data(), 96000, out.data(), 60000);
        QVERIFY(std::abs(int(got) - 48000) <= 1);
        out.resize(got);
        QVERIFY(std::abs(frequency(out, 48000.0) - 1000.0) < 5.0);
    }

    void upsamplingKeepsThePitch()
    {
        LinearResampler r;
        r.setStep(16000.0 / 48000.0);
        const std::vector<float> in = sine(440.0, 16000.0, 16000);
        std::vector<float> out(60000);
        const std::uint32_t got = r.process(in.data(), 16000, out.data(), 60000);
        QVERIFY(std::abs(int(got) - 48000) <= 3);
        out.resize(got);
        QVERIFY(std::abs(frequency(out, 48000.0) - 440.0) < 5.0);
    }

    // The IO proc hands it whatever block size the device picked, so the
    // stream has to come out the same however it is chopped up.
    void blocksMatchOneShot()
    {
        const std::vector<float> in = sine(1234.0, 44100.0, 4410);
        const double step = 44100.0 / 48000.0;

        LinearResampler whole;
        whole.setStep(step);
        std::vector<float> a(8000);
        a.resize(whole.process(in.data(), 4410, a.data(), 8000));

        LinearResampler chunked;
        chunked.setStep(step);
        std::vector<float> b;
        float buf[1024];
        for (std::uint32_t at = 0; at < 4410;) {
            const std::uint32_t n = std::min<std::uint32_t>(4410 - at, 37 + at % 300);
            const std::uint32_t got = chunked.process(in.data() + at, n, buf, 1024);
            b.insert(b.end(), buf, buf + got);
            at += n;
        }
        // The one-shot run cannot reach past its last sample; the chunked one
        // stops at the same place.
        QCOMPARE(b.size(), a.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            QVERIFY(std::abs(a[i] - b[i]) < 1e-5f);
    }

    void capacityIsRespected()
    {
        LinearResampler r;
        r.setStep(0.25);
        const std::vector<float> in = sine(100.0, 12000.0, 1000);
        float out[64];
        QCOMPARE(r.process(in.data(), 1000, out, 64), 64u);
        // And the next block still starts cleanly.
        QVERIFY(r.process(in.data(), 1000, out, 64) == 64u);
    }
};

QTEST_GUILESS_MAIN(TstResample)
#include "tst_resample.moc"
