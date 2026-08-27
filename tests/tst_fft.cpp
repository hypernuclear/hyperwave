#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include <QTest>

#include "dsp/Fft.h"
#include "dsp/Window.h"

using namespace hyperwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Deliberately the textbook O(N^2) transform. It is slow and obviously
// correct, which is exactly what a reference is for -- if the fast one
// disagrees with this, the fast one is wrong.
std::vector<std::complex<double>> naiveDft(const std::vector<float> &x)
{
    const int n = static_cast<int>(x.size());
    std::vector<std::complex<double>> out(static_cast<std::size_t>(n / 2 + 1));
    for (int k = 0; k <= n / 2; ++k) {
        std::complex<double> acc{0.0, 0.0};
        for (int t = 0; t < n; ++t) {
            const double a = -2.0 * kPi * k * t / n;
            acc += std::complex<double>(x[static_cast<std::size_t>(t)] * std::cos(a),
                                        x[static_cast<std::size_t>(t)] * std::sin(a));
        }
        out[static_cast<std::size_t>(k)] = acc;
    }
    return out;
}

} // namespace

class TstFft : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The test that catches every sign error, every twiddle-index slip and
    // every misplaced factor of two in the real-FFT unpacking.
    void matchesNaiveDft()
    {
        constexpr int n = 64;
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        std::vector<float> x(n);
        for (auto &v : x)
            v = dist(rng);

        const auto reference = naiveDft(x);

        // windowSum of 2 makes the renderer's normalisation a no-op for the
        // interior bins, so magnitudes compare directly against the DFT.
        Fft fft(n);
        std::vector<float> power(static_cast<std::size_t>(fft.bins()));
        fft.powerSpectrum(x.data(), 2.0f, power.data());

        for (int k = 0; k <= n / 2; ++k) {
            const double refMag = std::abs(reference[static_cast<std::size_t>(k)]);
            // DC and Nyquist are single-sided already and carry half the scale.
            const double expected = (k == 0 || k == n / 2) ? refMag * 0.5 : refMag;
            const double got = std::sqrt(static_cast<double>(power[static_cast<std::size_t>(k)]));
            QVERIFY2(std::abs(got - expected) < 1e-4,
                     qPrintable(QStringLiteral("bin %1: got %2 expected %3")
                                    .arg(k).arg(got).arg(expected)));
        }
    }

    // The normalisation anchor every dB constant in DspConfig depends on: a
    // full-scale sine sitting exactly on a bin centre must read 1.0.
    void fullScaleSineReadsUnity()
    {
        constexpr int n = 1024;
        constexpr int bin = 64;

        HannWindow window(n);
        std::vector<float> x(n), windowed(n);
        for (int i = 0; i < n; ++i)
            x[static_cast<std::size_t>(i)] = std::sin(2.0 * kPi * bin * i / n);
        window.apply(x.data(), windowed.data());

        Fft fft(n);
        std::vector<float> power(static_cast<std::size_t>(fft.bins()));
        fft.powerSpectrum(windowed.data(), window.sum(), power.data());

        const double mag = std::sqrt(static_cast<double>(power[bin]));
        QVERIFY2(std::abs(mag - 1.0) < 0.02,
                 qPrintable(QStringLiteral("full-scale sine read %1, expected 1.0").arg(mag)));
    }

    void silenceIsSilent()
    {
        constexpr int n = 1024;
        Fft fft(n);
        std::vector<float> x(n, 0.0f);
        std::vector<float> power(static_cast<std::size_t>(fft.bins()), 1.0f);
        fft.powerSpectrum(x.data(), 512.0f, power.data());
        for (int k = 0; k < fft.bins(); ++k)
            QVERIFY(power[static_cast<std::size_t>(k)] < 1e-12f);
    }

    void hannWindowSumsToHalfN()
    {
        // The FFT normalisation assumes exactly this, so it is worth asserting
        // rather than trusting: a symmetric Hann would sum to (N-1)/2 and
        // every level in the app would sit slightly wrong.
        HannWindow w(2048);
        QVERIFY(std::abs(w.sum() - 1024.0f) < 0.01f);
    }
};

QTEST_APPLESS_MAIN(TstFft)
#include "tst_fft.moc"
