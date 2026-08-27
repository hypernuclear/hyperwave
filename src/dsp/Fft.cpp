#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hyperwave {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Fft::Fft(int n)
    : m_n(n)
    , m_half(n / 2)
{
    // Bit-reversal permutation for the m_half-point complex transform.
    m_bitrev.resize(static_cast<std::size_t>(m_half));
    int bits = 0;
    while ((1 << bits) < m_half)
        ++bits;
    for (int i = 0; i < m_half; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        }
        m_bitrev[static_cast<std::size_t>(i)] = r;
    }

    m_cos.resize(m_half / 2 > 0 ? static_cast<std::size_t>(m_half / 2) : 1);
    m_sin.resize(m_cos.size());
    for (std::size_t i = 0; i < m_cos.size(); ++i) {
        const double a = -2.0 * kPi * static_cast<double>(i) / static_cast<double>(m_half);
        m_cos[i] = static_cast<float>(std::cos(a));
        m_sin[i] = static_cast<float>(std::sin(a));
    }

    // Twiddles for splitting the packed complex result back into the real
    // spectrum. Indexed 0..m_half inclusive.
    m_splitCos.resize(static_cast<std::size_t>(m_half) + 1);
    m_splitSin.resize(m_splitCos.size());
    for (int i = 0; i <= m_half; ++i) {
        const double a = -kPi * static_cast<double>(i) / static_cast<double>(m_half);
        m_splitCos[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(a));
        m_splitSin[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(a));
    }

    m_re.resize(static_cast<std::size_t>(m_half));
    m_im.resize(static_cast<std::size_t>(m_half));
}

void Fft::complexFft(float *re, float *im) const
{
    // Decimation in time, iterative Cooley-Tukey. Inputs are permuted in place
    // first so the butterflies below run over contiguous, in-order data.
    for (int i = 0; i < m_half; ++i) {
        const int j = m_bitrev[static_cast<std::size_t>(i)];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int len = 2; len <= m_half; len <<= 1) {
        const int half = len / 2;
        const int step = m_half / len;
        for (int i = 0; i < m_half; i += len) {
            for (int k = 0; k < half; ++k) {
                const auto t = static_cast<std::size_t>(k * step);
                const float wr = m_cos[t];
                const float wi = m_sin[t];
                const int a = i + k;
                const int b = a + half;
                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

void Fft::powerSpectrum(const float *in, float windowSum, float *outPower)
{
    // Pack the N real samples as N/2 complex ones: even samples become the real
    // part, odd samples the imaginary part. One half-length complex transform
    // then does the work of a full-length real one.
    for (int i = 0; i < m_half; ++i) {
        m_re[static_cast<std::size_t>(i)] = in[2 * i];
        m_im[static_cast<std::size_t>(i)] = in[2 * i + 1];
    }

    complexFft(m_re.data(), m_im.data());

    // Unpack. Z is the packed transform; the real spectrum X[k] is recovered
    // from the Hermitian-symmetric even part and the anti-symmetric odd part of
    // Z[k] against Z[N/2 - k], with Z[N/2] aliasing back to Z[0].
    //
    // Scale so a full-scale sine at a bin centre reads 1.0: the single-sided
    // magnitude is 2*|X[k]|, and coherent window gain divides it by sum(w).
    const float scale = (windowSum > 0.0f) ? (2.0f / windowSum) : 1.0f;

    for (int k = 0; k <= m_half; ++k) {
        const auto a = static_cast<std::size_t>((k == m_half) ? 0 : k);
        const auto b = static_cast<std::size_t>((k == 0 || k == m_half) ? 0 : (m_half - k));

        const float ar = m_re[a], ai = m_im[a];
        const float br = m_re[b], bi = m_im[b];

        const float er = 0.5f * (ar + br);
        const float ei = 0.5f * (ai - bi);
        const float orr = 0.5f * (ai + bi);
        const float oi = -0.5f * (ar - br);

        const float wr = m_splitCos[static_cast<std::size_t>(k)];
        const float wi = m_splitSin[static_cast<std::size_t>(k)];

        const float xr = er + (orr * wr - oi * wi);
        const float xi = ei + (orr * wi + oi * wr);

        // DC and Nyquist are real and single-sided already, so they must not
        // pick up the factor of two the interior bins need.
        float mag;
        if (k == 0 || k == m_half)
            mag = std::abs(xr) * scale * 0.5f;
        else
            mag = std::sqrt(xr * xr + xi * xi) * scale;

        outPower[k] = mag * mag;
    }
}

} // namespace hyperwave
