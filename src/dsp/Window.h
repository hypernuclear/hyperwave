#pragma once

#include <vector>

namespace hyperlight {

// Periodic Hann window plus its coherent gain.
//
// Periodic (divide by N) rather than symmetric (divide by N-1): the periodic
// form is the one whose bins land cleanly for spectral analysis, and it makes
// sum(w) exactly N/2, which the FFT normalisation relies on.
class HannWindow
{
public:
    HannWindow() = default;
    explicit HannWindow(int n) { resize(n); }

    void resize(int n);

    // Multiplies `in` by the window into `out`. Aliasing is allowed.
    void apply(const float *in, float *out) const;

    int size() const { return static_cast<int>(m_w.size()); }
    float sum() const { return m_sum; }
    const float *data() const { return m_w.data(); }

private:
    std::vector<float> m_w;
    float m_sum = 0.0f;
};

} // namespace hyperlight
