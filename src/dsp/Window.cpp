#include "dsp/Window.h"

#include <cmath>

namespace hyperlight {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void HannWindow::resize(int n)
{
    if (n == static_cast<int>(m_w.size()))
        return;
    m_w.resize(static_cast<std::size_t>(n));
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v =
            0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n)));
        m_w[static_cast<std::size_t>(i)] = static_cast<float>(v);
        sum += v;
    }
    m_sum = static_cast<float>(sum);
}

void HannWindow::apply(const float *in, float *out) const
{
    const std::size_t n = m_w.size();
    for (std::size_t i = 0; i < n; ++i)
        out[i] = in[i] * m_w[i];
}

} // namespace hyperlight
