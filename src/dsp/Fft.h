#pragma once

#include <vector>

namespace hyperwave {

// Real-input FFT, power-of-two sizes, producing a normalised power spectrum.
//
// Hand-written rather than vendored. At N=4096 this is roughly 135k flops per
// frame and we run 60 frames a second: about 8 Mflop/s, a rounding error on
// any machine that can run a compositor. KissFFT would have added a
// third_party tree, a notices entry and an update burden to replace something
// that fits on two screens and is checked against a naive DFT in the tests.
//
// Accelerate/vDSP would be faster still and is deliberately not used: this
// file is in the shared layer, and two FFT implementations across two
// platforms is how you end up debugging "the bars look different on Windows".
class Fft
{
public:
    explicit Fft(int n);

    // `in` holds size() real samples, already windowed.
    //
    // `outPower` receives bins() values normalised so that a full-scale sine
    // sitting on a bin centre yields exactly 1.0. That anchor is what makes the
    // dB constants in DspConfig mean something absolute; change the
    // normalisation and every threshold downstream silently shifts.
    //
    // `windowSum` is the sum of the analysis window's coefficients (N/2 for a
    // periodic Hann), used for coherent-gain correction.
    void powerSpectrum(const float *in, float windowSum, float *outPower);

    int size() const { return m_n; }
    int bins() const { return m_n / 2 + 1; }

private:
    void complexFft(float *re, float *im) const;   // in place, m_half points

    int m_n = 0;
    int m_half = 0;
    std::vector<int> m_bitrev;
    std::vector<float> m_cos, m_sin;             // butterfly twiddles
    std::vector<float> m_splitCos, m_splitSin;   // real-FFT unpacking twiddles
    std::vector<float> m_re, m_im;               // scratch, allocated once
};

} // namespace hyperwave
