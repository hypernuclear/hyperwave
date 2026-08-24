#include "dsp/BandMapper.h"

#include <algorithm>
#include <cmath>

#include "dsp/DspConfig.h"

namespace hyperlight {

namespace {

// Gentle treble lift plus a small shelf under the bass, relative to 1 kHz.
//
// This is purely aesthetic. The band aggregation below already produces a flat
// display for pink noise, so nothing here is correcting an error -- it exists
// because real music rolls off faster than pink up top and runs hotter than
// pink down low, and matching that is what makes the strip look right.
float tiltFor(float centreHz)
{
    float db = dsp::kTiltDbPerOctave * std::log2(centreHz / 1000.0f);
    if (centreHz < dsp::kBassShelfHz) {
        db += dsp::kBassShelfDb;
    } else if (centreHz < dsp::kBassShelfEndHz) {
        const float t = (centreHz - dsp::kBassShelfHz)
                      / (dsp::kBassShelfEndHz - dsp::kBassShelfHz);
        db += dsp::kBassShelfDb * (1.0f - t);
    }
    return db;
}

} // namespace

void BandMapper::rebuild(int bandCount, int fftSize, double sampleRate, float loHz, float hiHz)
{
    m_bands.clear();
    if (bandCount <= 0 || fftSize <= 0 || sampleRate <= 0.0)
        return;

    const int binCount = fftSize / 2 + 1;
    const auto hzPerBin = static_cast<float>(sampleRate / static_cast<double>(fftSize));

    m_binMin = std::max(1, static_cast<int>(std::ceil(dsp::kAnalysisLoHz / hzPerBin)));
    m_binMax = std::min(binCount - 1, static_cast<int>(std::floor(dsp::kAnalysisHiHz / hzPerBin)));

    m_bands.resize(static_cast<std::size_t>(bandCount));
    const float ratio = hiHz / loHz;

    for (int k = 0; k < bandCount; ++k) {
        const float lo = loHz * std::pow(ratio, static_cast<float>(k) / static_cast<float>(bandCount));
        const float hi = loHz * std::pow(ratio, static_cast<float>(k + 1) / static_cast<float>(bandCount));

        Band b;
        b.centreHz = std::sqrt(lo * hi);
        b.tiltDb = tiltFor(b.centreHz);
        b.binLo = static_cast<int>(std::ceil(lo / hzPerBin));
        b.binHi = static_cast<int>(std::floor(hi / hzPerBin));
        b.binLo = std::max(b.binLo, m_binMin);
        b.binHi = std::min(b.binHi, m_binMax);
        // At 48 kHz with a 4096-point window the bins are 11.7 Hz wide, so the
        // bottom bands (40-61 Hz) are narrower than a single bin and come back
        // empty. Interpolating the spectrum at the band centre keeps them alive
        // instead of pinning them to the floor.
        b.interpBin = b.centreHz / hzPerBin;
        m_bands[static_cast<std::size_t>(k)] = b;
    }
}

void BandMapper::aggregate(const float *power, float *outDb) const
{
    // SUM the bin powers in each band -- do not average them.
    //
    // This is easy to get backwards and it matters. Music is roughly pink
    // (power density proportional to 1/f) and geometrically spaced bands have
    // bandwidth proportional to f, so the sum over a band comes out roughly
    // constant across the spectrum: a flat display for pink noise, for free.
    // Averaging instead divides by a bin count that grows with f, which puts a
    // -3 dB/octave slope back in and makes the bass swamp everything -- damage
    // you would then have to bolt on a tilt to undo.
    const int n = static_cast<int>(m_bands.size());
    for (int k = 0; k < n; ++k) {
        const Band &b = m_bands[static_cast<std::size_t>(k)];
        float p = 0.0f;

        if (b.binHi >= b.binLo) {
            for (int i = b.binLo; i <= b.binHi; ++i)
                p += power[i];
        } else {
            const int i0 = static_cast<int>(b.interpBin);
            const int i1 = i0 + 1;
            if (i0 >= m_binMin && i1 <= m_binMax) {
                const float t = b.interpBin - static_cast<float>(i0);
                p = power[i0] * (1.0f - t) + power[i1] * t;
            } else if (i0 >= m_binMin && i0 <= m_binMax) {
                p = power[i0];
            }
        }

        outDb[k] = 10.0f * std::log10(p + 1e-12f) + b.tiltDb;
    }
}

} // namespace hyperlight
